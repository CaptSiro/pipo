/*
 * The C half of lib/fs.pp: files and directories, modelled on Java's
 * java.io.File and its FileInputStream / FileOutputStream.
 *
 *     import fs
 *     const f = fs:file("data", "order.txt")         // just a path: nothing is opened yet
 *     if success(text) = f.read_text() { ... }
 *     const input = f.open_read().unwrap()
 *     const buf = types:int8array(4096)
 *     let n = input.read(buf)                         // bytes read, -1 at the end
 *     input.close()
 *
 * This file is also the worked example of a mixed module (.pp + C) with its
 * own object types; md/NATIVE_MODULES.md walks through it step by step. In
 * short:
 *
 *   1. lib/fs.pp is the module. It declares the types (`native type File`), the
 *      signature of every function (`native "f_read" function read(File) -> ...`)
 *      and the constants, and exports them. Overload resolution has checked the
 *      arguments against those signatures before any function here runs.
 *   2. Each type is a NativeClass (the TYPES table at the end): its name, the
 *      size of its C struct and the hooks the interpreter calls (free, format,
 *      equals). A value of the type is that struct, starting with ObjInstance.
 *   3. Each function is a NativeFn (the FS table at the end), found by the name
 *      written after `native` in fs.pp, or by the function's own name.
 *   4. mod_fs_register (called from vm_new in src/interp.c, declared in vm.h)
 *      registers both tables under the library module name "fs".
 *
 * File and Directory hold a path; their functions look at the file system each
 * time they run. Contents are bytes in a types:Int8Array (see src/modules/types.c).
 * Operations that can fail for reasons outside the program (opening, reading a
 * whole file, listing a directory) return Error values; simple queries return
 * bool / int like Java does. Misusing a value (reading a closed stream) is a
 * runtime error.
 *
 * Paths are passed to the C library as they are: narrow (char) strings, which
 * Windows reads in the ANSI code page.
 */
#include <errno.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "vm.h"

#ifdef _WIN32
#include <direct.h>
#include <io.h>
#define PP_SEP '\\'
#define PP_SEP_STR "\\"
#define PP_LIST_SEP_STR ";"
typedef struct _stat64 pp_stat_t;
#define pp_stat(path, st) _stat64(path, st)
#define pp_mkdir(path) _mkdir(path)
#define pp_rmdir(path) _rmdir(path)
#define pp_getcwd(buf, size) _getcwd(buf, size)
#else
#include <dirent.h>
#include <unistd.h>
#define PP_SEP '/'
#define PP_SEP_STR "/"
#define PP_LIST_SEP_STR ":"
typedef struct stat pp_stat_t;
#define pp_stat(path, st) stat(path, st)
#define pp_mkdir(path) mkdir(path, 0777)
#define pp_rmdir(path) rmdir(path)
#define pp_getcwd(buf, size) getcwd(buf, size)
#endif

/* ------------------------------------------------------------------ paths
 *
 * Plain C helpers on char* paths, shared by everything below. Both / and \ count
 * as separators on every platform; separators this file adds use PP_SEP.
 */

static bool is_sep(char c) {
    return c == '/' || c == '\\';
}

static bool path_is_absolute(const char* p) {
    if (is_sep(p[0])) {
        return true;
    }

    return ((p[0] >= 'a' && p[0] <= 'z') || (p[0] >= 'A' && p[0] <= 'Z')) && p[1] == ':';
}

/* base + one separator + name (malloc'd); an absolute name or an empty base gives just name */
static char* path_join(const char* base, const char* name) {
    size_t bl = strlen(base), nl = strlen(name);
    if (!bl || path_is_absolute(name)) {
        return pp_strndup(name, nl);
    }
    while (nl && is_sep(*name)) {
        name++, nl--;
    }

    const bool sep = is_sep(base[bl - 1]);
    char* r = pp_malloc(bl + nl + 2);
    memcpy(r, base, bl);
    if (!sep) {
        r[bl++] = PP_SEP;
    }

    memcpy(r + bl, name, nl + 1);

    return r;
}

/* length of p without trailing separators, keeping a lone root ("/", "C:\") */
static size_t trimmed_len(const char* p) {
    size_t n = strlen(p);
    const size_t root = is_sep(p[0]) ? 1 : (n >= 3 && p[1] == ':' && is_sep(p[2])) ? 3 : 0;
    while (n > root && is_sep(p[n - 1])) {
        n--;
    }

    return n;
}

/* the start of the last part of p (n = trimmed length) */
static size_t last_part(const char* p, size_t n) {
    size_t i = n;
    while (i > 0 && !is_sep(p[i - 1]) && !(i == 2 && p[1] == ':')) {
        i--;
    }

    return i;
}

static int stat_path(const char* path, pp_stat_t* st) {
    return pp_stat(path, st);
}

static bool path_exists(const char* p) {
    pp_stat_t st;
    return stat_path(p, &st) == 0;
}

static bool path_is_dir(const char* p) {
    pp_stat_t st;
    return stat_path(p, &st) == 0 && (st.st_mode & S_IFMT) == S_IFDIR;
}

static bool path_is_file(const char* p) {
    pp_stat_t st;
    return stat_path(p, &st) == 0 && (st.st_mode & S_IFMT) == S_IFREG;
}

/* an absolute form of p (malloc'd) */
static char* path_absolute(const char* p) {
#ifdef _WIN32
    char* r = _fullpath(NULL, p, 0);
    if (r) {
        return r;
    }
#else
    char* r = realpath(p, NULL);
    if (r) {
        return r;
    }

    if (path_is_absolute(p)) {
        return pp_strdup(p);
    }
#endif
    char cwd[4096];
    if (!pp_getcwd(cwd, sizeof cwd)) {
        return pp_strdup(p);
    }

    return path_join(cwd, p);
}

/* ------------------------------------------------- File and Directory
 *
 * The instance struct of both types. It must start with ObjInstance (the GC
 * header plus the instance's type); the class's `size` (sizeof(ObjPath)) tells
 * instance_new how much to allocate. Everything after the header belongs to
 * this file: here a malloc'd copy of the path.
 *
 * An instance may only hold plain C data. The collector never looks inside
 * it, so a pointer to an ObjString or another object would not keep that
 * object alive. That is why the path is copied out of the argument string
 * instead of keeping the ObjString*.
 */

typedef struct {
    ObjInstance base;
    char* path; /* owned: malloc'd here, freed by path_free */
} ObjPath;

/* the path of a File or Directory argument (fs.pp guarantees the type) */
#define PATH_OF(v) (((ObjPath*)(v).as.o)->path)

/*
 * The ObjNativeType of one of this module's types. vm_register_native_types
 * created them when the interpreter started, so they exist before lib/fs.pp
 * is loaded; `native type File` in fs.pp only binds the name.
 */
static ObjNativeType* fs_type(VM* vm, const char* name) {
    return vm_native_type(vm, "fs", name);
}

/*
 * Allocation: instance_new allocates `size` bytes, zeroes everything after
 * the GC header, links the object into the GC's list and sets its type. The
 * fields are then filled in. No collection can run while a native function
 * runs (the GC only runs between statements), so the new object is safe until
 * it is returned, even though nothing references it yet.
 */
static Value path_new(VM* vm, const char* type, char* path /* taken */) {
    ObjPath* o = (ObjPath*)instance_new(vm, fs_type(vm, type));
    o->path = path;

    return obj_val(o);
}

static Value new_file(VM* vm, char* path) {
    return path_new(vm, "File", path);
}

static Value new_dir(VM* vm, char* path) {
    return path_new(vm, "Directory", path);
}

/*
 * The constructors: file(path), file(parent, name) and the same for directory.
 * A native type is never called (fs:File("x") is an error naming the class's
 * `constructor`), so making values is an ordinary function. fs.pp declares
 * three overloads of each, (string), (string, string) and (Directory, string),
 * all bound to this one C function, which tells them apart by argc and the
 * argument kinds. The path is kept as written; absolute() resolves it.
 * d.file(name) is file(d, name): a dot call passes the receiver first.
 */
static Value path_make(VM* vm, const char* type, int argc, Value* a) {
    if (argc == 1) {
        return path_new(vm, type, pp_strdup(AS_STR(a[0])->chars));
    }

    const char* base = IS_KIND(a[0], OBJ_STRING) ? AS_STR(a[0])->chars : PATH_OF(a[0]);

    return path_new(vm, type, path_join(base, AS_STR(a[1])->chars));
}

static Value fs_file(VM* vm, int argc, Value* a) {
    return path_make(vm, "File", argc, a);
}

static Value fs_directory(VM* vm, int argc, Value* a) {
    return path_make(vm, "Directory", argc, a);
}

/*
 * Deallocation: the `free` hook. The collector calls it when it finds the
 * instance unreachable, just before it frees the object itself, so it
 * releases only what the instance owns, never `self`.
 */
static void path_free(ObjInstance* self) {
    free(((ObjPath*)self)->path);
}

/* the `format` hook: io:print, str() and nested printing show File("data/order.txt") */
static void path_format(StrBuf* sb, ObjInstance* self) {
    sb_printf(sb, "%s(\"%s\")", self->type->name->chars, ((ObjPath*)self)->path);
}

/*
 * the `equals` hook: a == b for two values of the same class (a File is never
 * equal to a Directory). Without it, == would compare identity.
 */
static bool path_equals(ObjInstance* a, ObjInstance* b) {
    return strcmp(((ObjPath*)a)->path, ((ObjPath*)b)->path) == 0;
}

/* ------------------------------------------------------------- streams
 *
 * InputStream and OutputStream share one struct: an open C FILE* and the path
 * it was opened from (for messages). Only open_read / open_write / open_append
 * make them (see open_with), so a stream is open from the start; close() sets
 * fp to NULL, and every function that uses the file goes through open_stream,
 * which turns a closed stream into a runtime error.
 */

typedef struct {
    ObjInstance base;
    FILE* fp;    /* owned unless is_std; NULL once closed */
    char* path;  /* owned */
    bool is_std; /* stdin, stdout or stderr: never fclosed, close() only detaches it */
} ObjStream;

#define STREAM(v) ((ObjStream*)(v).as.o)

static Value stream_make(VM* vm, const char* type, FILE* fp, const char* path, bool is_std) {
    ObjStream* s = (ObjStream*)instance_new(vm, fs_type(vm, type));
    s->fp = fp;
    s->path = pp_strdup(path);
    s->is_std = is_std;

    return obj_val(s);
}

static Value stream_new(VM* vm, const char* type, FILE* fp, const char* path) {
    return stream_make(vm, type, fp, path, false);
}

/* fs:stdin, fs:stdout, fs:stderr: the same FILE* objects the io module uses */
static Value in_stdin(VM* vm, int argc, Value* a) {
    return stream_make(vm, "InputStream", stdin, "pp://stdin", true);
}

static Value out_stdout(VM* vm, int argc, Value* a) {
    return stream_make(vm, "OutputStream", stdout, "pp://stdout", true);
}

static Value out_stderr(VM* vm, int argc, Value* a) {
    return stream_make(vm, "OutputStream", stderr, "pp://stderr", true);
}

/*
 * Closing through the GC: a stream that becomes unreachable while still open
 * is closed here, when the collector frees it (like a Java finalizer /
 * Cleaner). That is a safety net, not a way to manage files:
 *   - the collector runs between statements, only once enough objects were
 *     allocated, so an unclosed stream may stay open for a long time, and a
 *     loop that opens files without close() runs out of file handles first;
 *   - an error from fclose here is lost: nobody is left to report it to.
 * At the end of the program the interpreter exits without freeing objects;
 * the C library then flushes and closes every FILE* still open, so an
 * OutputStream that was never closed still has its data written.
 */
static void stream_free(ObjInstance* self) {
    const ObjStream* s = (ObjStream*)self;
    if (s->fp && !s->is_std) {
        fclose(s->fp);
    }

    free(s->path);
}

/* InputStream("data.bin"), and InputStream("data.bin", closed) after close() */
static void stream_format(StrBuf* sb, ObjInstance* self) {
    const ObjStream* s = (ObjStream*)self;
    sb_printf(sb, "%s(\"%s\"%s)", self->type->name->chars, s->path, s->fp ? "" : ", closed");
}

/* the FILE* of a stream argument; a runtime error when it was closed */
static FILE* open_stream(VM* vm, Value v) {
    const ObjStream* s = STREAM(v);
    if (!s->fp) {
        rt_error(vm, 0, "%s of '%s' is closed", s->base.type->name->chars, s->path);
    }

    return s->fp;
}

/* ------------------------------------------------- results and errors
 *
 * Two ways to fail:
 *   - an Error value, fail("cannot open 'x': No such file or directory"),
 *     for what the program cannot prevent (a missing file, no permission).
 *     The caller decides with match / unwrap / is_fail. make_success and
 *     make_fail (interp.c) build the prelude's Error<S, F> values; the
 *     declared return type in fs.pp (Error<int>) is checked against them.
 *   - rt_error, which prints file:line: error: ... and a traceback and ends
 *     the program, for mistakes in the program (a closed stream, an offset
 *     outside the buffer). It does not return.
 * These helpers build the message from errno right away, before another
 * library call can change it.
 */

static Value fail_errno(VM* vm, const char* what, const char* path) {
    StrBuf sb;
    sb_init(&sb);
    sb_printf(&sb, "cannot %s '%s': %s", what, path, strerror(errno));
    const Value msg = obj_val(str_new(vm, sb.data, (int)sb.len));
    sb_free(&sb);

    return make_fail(vm, msg);
}

static Value fail_msg(VM* vm, const char* what, const char* path, const char* why) {
    StrBuf sb;
    sb_init(&sb);
    sb_printf(&sb, "cannot %s '%s': %s", what, path, why);
    const Value msg = obj_val(str_new(vm, sb.data, (int)sb.len));
    sb_free(&sb);

    return make_fail(vm, msg);
}

/* --------------------------------------------- functions on plain paths
 *
 * Every native function has the same C signature (NativeFn): the VM, the
 * number of arguments and the arguments. Because fs.pp declared
 * exists(string path) -> bool, a[0] is known to be a string here, and the
 * result is checked against bool after the function returns. Values are made
 * with bool_val, int_val, float_val, obj_val(str_cnew(...)) and so on
 * (value.h).
 */

static Value fs_exists(VM* vm, int argc, Value* a) {
    return bool_val(path_exists(AS_STR(a[0])->chars));
}

static Value fs_is_dir(VM* vm, int argc, Value* a) {
    return bool_val(path_is_dir(AS_STR(a[0])->chars));
}

static Value fs_is_file(VM* vm, int argc, Value* a) {
    return bool_val(path_is_file(AS_STR(a[0])->chars));
}

/*
 * The values of the module's constants, fs:separator and fs:path_separator.
 * fs.pp declares these as private natives (no `export`) and evaluates
 * `export const separator = name_separator()` once while it loads.
 */
static Value fs_name_separator(VM* vm, int argc, Value* a) {
    return obj_val(str_cnew(vm, PP_SEP_STR));
}

static Value fs_list_separator(VM* vm, int argc, Value* a) {
    return obj_val(str_cnew(vm, PP_LIST_SEP_STR));
}

/* cwd() -> Directory: the working directory */
static Value fs_cwd(VM* vm, int argc, Value* a) {
    char buf[4096];
    if (!pp_getcwd(buf, sizeof buf)) {
        rt_error(vm, 0, "cannot read the working directory: %s", strerror(errno));
    }

    return new_dir(vm, pp_strdup(buf));
}

/* ------------------------------------------- shared by File and Directory
 *
 * fs.pp declares these twice, path(File) and path(Directory), both bound to
 * "p_path": several declarations may share one C function. Both structs are
 * an ObjPath, so PATH_OF works for either.
 */

static Value p_path(VM* vm, int argc, Value* a) {
    return obj_val(str_cnew(vm, PATH_OF(a[0])));
}

/* the last part of the path: "order.txt" for data/order.txt */
static Value p_name(VM* vm, int argc, Value* a) {
    const char* p = PATH_OF(a[0]);
    const size_t n = trimmed_len(p);
    const size_t s = last_part(p, n);

    return obj_val(str_new(vm, p + s, (int)(n - s)));
}

/* parent() -> Directory?: the path without its last part, none() when there is none (like Java's getParent) */
static Value p_parent(VM* vm, int argc, Value* a) {
    const char* p = PATH_OF(a[0]);
    const size_t n = trimmed_len(p);
    const size_t s = last_part(p, n);
    if (s == 0 || s == n) {
        return make_none(vm);
    }

    size_t e = s;
    const size_t root = is_sep(p[0]) ? 1 : (p[1] == ':' ? (is_sep(p[2]) ? 3 : 2) : 0);
    while (e > root && is_sep(p[e - 1])) {
        e--;
    }

    return make_some(vm, new_dir(vm, pp_strndup(p, e)));
}

static Value p_exists(VM* vm, int argc, Value* a) {
    return bool_val(path_exists(PATH_OF(a[0])));
}

static Value p_absolute(VM* vm, int argc, Value* a) {
    return path_new(vm, AS_INSTANCE(a[0])->type->name->chars, path_absolute(PATH_OF(a[0])));
}

/* rename(f, to): false when it failed (like Java's renameTo); `to` is a string or the same kind of path */
static Value p_rename(VM* vm, int argc, Value* a) {
    const char* to = IS_KIND(a[1], OBJ_STRING) ? AS_STR(a[1])->chars : PATH_OF(a[1]);
    return bool_val(rename(PATH_OF(a[0]), to) == 0);
}

/* last modification time in milliseconds since 1970 (like Java's lastModified), 0 when missing */
static Value p_modified(VM* vm, int argc, Value* a) {
    pp_stat_t st;
    if (stat_path(PATH_OF(a[0]), &st) != 0) {
        return int_val(0);
    }

    return int_val((int64_t)st.st_mtime * 1000);
}

/* ------------------------------------------------------------------ File */

static Value f_is_file(VM* vm, int argc, Value* a) {
    return bool_val(path_is_file(PATH_OF(a[0])));
}

/* "txt" for order.txt, "" when there is none (a leading dot does not count: .gitignore has none) */
static Value f_extension(VM* vm, int argc, Value* a) {
    const char* p = PATH_OF(a[0]);
    const size_t n = trimmed_len(p);
    const size_t s = last_part(p, n);
    for (size_t i = n; i > s + 1; i--) {
        if (p[i - 1] == '.') {
            return obj_val(str_new(vm, p + i, (int)(n - i)));
        }
    }

    return obj_val(str_cnew(vm, ""));
}

/* size in bytes, 0 when the file does not exist (like Java's length) */
static Value f_size(VM* vm, int argc, Value* a) {
    pp_stat_t st;
    if (stat_path(PATH_OF(a[0]), &st) != 0) {
        return int_val(0);
    }

    return int_val((int64_t)st.st_size);
}

/*
 * Reading a whole file: read() and read_text().
 *
 * slurp opens the file, reads it into a growing malloc'd buffer and closes it
 * again, so nothing stays open whichever way it ends. On failure it returns
 * NULL with errno set and `what` naming the step that failed, which becomes
 * fail("cannot open 'x': ...") / fail("cannot read 'x': ...").
 */

static char* slurp(const char* path, int64_t* out_len, const char** what) {
    *what = "open";
    if (path_is_dir(path)) {
        errno = EISDIR;
        return NULL;
    }

    FILE* fp = fopen(path, "rb");
    if (!fp) {
        return NULL;
    }

    *what = "read";
    size_t cap = 1 << 16, len = 0;
    char* buf = pp_malloc(cap);
    for (;;) {
        if (len == cap) {
            cap *= 2;
            buf = pp_realloc(buf, cap);
        }

        const size_t got = fread(buf + len, 1, cap - len, fp);
        len += got;
        if (got == 0) {
            break;
        }
    }

    const bool bad = ferror(fp) != 0;
    const int err = errno;
    fclose(fp);
    if (bad) {
        free(buf);
        errno = err;
        return NULL;
    }

    *out_len = (int64_t)len;
    return buf;
}

/*
 * read(f) -> Error<types:Int8Array>: the whole contents. The Int8Array type
 * belongs to another module (types); typed_array_new (vm.h) makes one of it
 * from C. fs.pp imports types, so `types:Int8Array` in its signatures
 * resolves, and the return value is checked against it.
 */
static Value f_read(VM* vm, int argc, Value* a) {
    int64_t len;
    const char* what;
    char* buf = slurp(PATH_OF(a[0]), &len, &what);
    if (!buf) {
        return fail_errno(vm, what, PATH_OF(a[0]));
    }

    ObjTypedArray* r = typed_array_new(vm, TA_I8, len);
    memcpy(r->data, buf, (size_t)len);
    free(buf);

    return make_success(vm, obj_val(r));
}

/* read_text(f) -> Error<string>: the whole contents as UTF-8 text */
static Value f_read_text(VM* vm, int argc, Value* a) {
    int64_t len;
    const char* what;
    char* buf = slurp(PATH_OF(a[0]), &len, &what);
    if (!buf) {
        return fail_errno(vm, what, PATH_OF(a[0]));
    }

    if (len > INT32_MAX) {
        free(buf);
        return fail_msg(vm, "read", PATH_OF(a[0]), "too large for a string (use read)");
    }

    const Value s = obj_val(str_new(vm, buf, (int)len));
    free(buf);

    return make_success(vm, s);
}

/*
 * The bytes of a string or Int8Array argument: write and append are declared
 * for both (write(File, string), write(File, types:Int8Array)), so the C side
 * only needs to know which one arrived.
 */
static const void* bytes_of(VM* vm, Value v, size_t* len) {
    if (IS_KIND(v, OBJ_STRING)) {
        *len = (size_t)AS_STR(v)->len;
        return AS_STR(v)->chars;
    }

    const ObjTypedArray* t = (ObjTypedArray*)v.as.o;
    *len = (size_t)t->length;
    return t->data;
}

/*
 * Writing a whole file: open, write, close, in one call. fclose flushes the C
 * library's buffer, so a full disk can show up only there; its result counts.
 */
static Value write_whole(VM* vm, Value* a, const char* mode) {
    const char* path = PATH_OF(a[0]);
    size_t len;
    const void* data = bytes_of(vm, a[1], &len);
    FILE* fp = fopen(path, mode);
    if (!fp) {
        return fail_errno(vm, "open", path);
    }

    const size_t put = fwrite(data, 1, len, fp);
    const int err = errno;
    if (fclose(fp) != 0 || put != len) {
        errno = err ? err : EIO;
        return fail_errno(vm, "write", path);
    }

    return make_success(vm, int_val((int64_t)put));
}

/* write(f, data) -> Error<int>: replaces the contents with a string or Int8Array; the bytes written */
static Value f_write(VM* vm, int argc, Value* a) {
    return write_whole(vm, a, "wb");
}

/* append(f, data) -> Error<int>: adds to the end, creating the file when missing */
static Value f_append(VM* vm, int argc, Value* a) {
    return write_whole(vm, a, "ab");
}

/* create(f) -> Error<bool>: makes an empty file; false when it already existed (like Java's createNewFile) */
static Value f_create(VM* vm, int argc, Value* a) {
    const char* path = PATH_OF(a[0]);
    if (path_exists(path)) {
        return make_success(vm, bool_val(false));
    }

    FILE* fp = fopen(path, "wb");
    if (!fp) {
        return fail_errno(vm, "create", path);
    }

    fclose(fp);

    return make_success(vm, bool_val(true));
}

/* delete(f) -> bool: removes the file; false when that failed */
static Value f_delete(VM* vm, int argc, Value* a) {
    const char* path = PATH_OF(a[0]);
    if (path_is_dir(path)) {
        return bool_val(false);
    }

    return bool_val(remove(path) == 0);
}

/* copy_to(f, to) -> Error<int>: copies the contents to another file (replacing it); the bytes copied */
static Value f_copy_to(VM* vm, int argc, Value* a) {
    const char* from = PATH_OF(a[0]);
    const char* to = IS_KIND(a[1], OBJ_STRING) ? AS_STR(a[1])->chars : PATH_OF(a[1]);
    FILE* in = fopen(from, "rb");
    if (!in) {
        return fail_errno(vm, "open", from);
    }

    FILE* out = fopen(to, "wb");
    if (!out) {
        const int err = errno;
        fclose(in);
        errno = err;
        return fail_errno(vm, "open", to);
    }

    char buf[1 << 16];
    int64_t total = 0;
    size_t got;
    bool ok = true;
    while ((got = fread(buf, 1, sizeof buf, in)) > 0) {
        if (fwrite(buf, 1, got, out) != got) {
            ok = false;
            break;
        }

        total += (int64_t)got;
    }

    const int err = errno;
    ok = ok && !ferror(in);
    fclose(in);
    if (fclose(out) != 0) {
        ok = false;
    }

    if (!ok) {
        errno = err ? err : EIO;
        return fail_errno(vm, "copy to", to);
    }

    return make_success(vm, int_val(total));
}

/*
 * Opening a stream: open_read ("rb"), open_write ("wb", replaces the file) and
 * open_append ("ab"). Binary modes, so Windows never translates line endings.
 * A file that cannot be opened is an Error value; on success the FILE* is
 * handed to a new InputStream / OutputStream, which owns it from then on.
 */
static Value open_with(VM* vm, Value file, const char* mode, const char* type) {
    const char* path = PATH_OF(file);
    if (path_is_dir(path)) {
        errno = EISDIR;
        return fail_errno(vm, "open", path);
    }

    FILE* fp = fopen(path, mode);
    if (!fp) {
        return fail_errno(vm, "open", path);
    }

    return make_success(vm, stream_new(vm, type, fp, path));
}

static Value f_open_read(VM* vm, int argc, Value* a) {
    return open_with(vm, a[0], "rb", "InputStream");
}

static Value f_open_write(VM* vm, int argc, Value* a) {
    return open_with(vm, a[0], "wb", "OutputStream");
}

static Value f_open_append(VM* vm, int argc, Value* a) {
    return open_with(vm, a[0], "ab", "OutputStream");
}

/* ------------------------------------------------------------- Directory */

static Value d_is_dir(VM* vm, int argc, Value* a) {
    return bool_val(path_is_dir(PATH_OF(a[0])));
}

/* create(d) -> bool: makes the directory (its parent must exist), false when that failed or it existed (Java's mkdir) */
static Value d_create(VM* vm, int argc, Value* a) {
    return bool_val(pp_mkdir(PATH_OF(a[0])) == 0);
}

/* create_all(d) -> bool: makes the directory and any missing parents; true when it exists afterwards (Java's mkdirs) */
static Value d_create_all(VM* vm, int argc, Value* a) {
    const char* p = PATH_OF(a[0]);
    if (path_is_dir(p)) {
        return bool_val(true);
    }

    const size_t n = trimmed_len(p);
    char* buf = pp_strndup(p, n);
    for (size_t i = 1; i <= n; i++) {
        if (i < n && !is_sep(buf[i])) {
            continue;
        }

        if (i < n && i > 0 && buf[i - 1] == ':') {
            continue; /* C:\ */
        }

        const char c = buf[i];
        buf[i] = '\0';
        if (!path_is_dir(buf)) {
            pp_mkdir(buf);
        }

        buf[i] = c;
    }

    free(buf);

    return bool_val(path_is_dir(p));
}

/* delete(d) -> bool: removes an empty directory; false when that failed */
static Value d_delete(VM* vm, int argc, Value* a) {
    return bool_val(pp_rmdir(PATH_OF(a[0])) == 0);
}


static int cmp_names(const void* x, const void* y) {
    return strcmp(*(char* const*)x, *(char* const*)y);
}

/*
 * The names in directory `path` (without . and ..), sorted; NULL with errno
 * set on failure. The caller frees each name and the array.
 */
static char** list_names(const char* path, int* count) {
    char** names = NULL;
    int n = 0, cap = 0;
    if (!path_is_dir(path)) {
        errno = path_exists(path) ? ENOTDIR : ENOENT;
        return NULL;
    }
#ifdef _WIN32
    char* pattern = path_join(path, "*");
    struct __finddata64_t fd;
    const intptr_t h = _findfirst64(pattern, &fd);
    free(pattern);
    if (h == -1) {
        if (errno != ENOENT) {
            return NULL;
        }
    } else {
        do {
            if (strcmp(fd.name, ".") == 0 || strcmp(fd.name, "..") == 0) {
                continue;
            }

            GROW(names, n, cap);
            names[n++] = pp_strdup(fd.name);
        } while (_findnext64(h, &fd) == 0);
        _findclose(h);
    }
#else
    const DIR* d = opendir(path);
    if (!d) {
        return NULL;
    }

    struct dirent* e;
    while ((e = readdir(d))) {
        if (strcmp(e->d_name, ".") == 0 || strcmp(e->d_name, "..") == 0) {
            continue;
        }

        GROW(names, n, cap);
        names[n++] = pp_strdup(e->d_name);
    }

    closedir(d);
#endif
    if (n > 1) {
        qsort(names, (size_t)n, sizeof(char*), cmp_names);
    }

    *count = n;
    if (!names) {
        names = pp_malloc(sizeof(char*)); /* an empty directory still succeeds */
    }

    return names;
}

/* 0: every entry as a string, 1: only files as File, 2: only directories as Directory */
static Value list_dir(VM* vm, Value dir, int mode) {
    const char* path = PATH_OF(dir);
    int n = 0;
    char** names = list_names(path, &n);
    if (!names) {
        return fail_errno(vm, "list", path);
    }

    ObjArray* r = array_new(vm, n);
    for (int i = 0; i < n; i++) {
        if (mode == 0) {
            r->items[r->count++] = obj_val(str_cnew(vm, names[i]));
        } else {
            char* full = path_join(path, names[i]);
            const bool d = path_is_dir(full);
            if (mode == 1 && !d) {
                r->items[r->count++] = new_file(vm, full);
            } else if (mode == 2 && d) {
                r->items[r->count++] = new_dir(vm, full);
            } else {
                free(full);
            }
        }

        free(names[i]);
    }

    free(names);

    return make_success(vm, obj_val(r));
}

/* list(d) -> Error<string[]>: the names of everything in d, sorted */
static Value d_list(VM* vm, int argc, Value* a) {
    return list_dir(vm, a[0], 0);
}

/* files(d) -> Error<File[]>: what is not a directory */
static Value d_files(VM* vm, int argc, Value* a) {
    return list_dir(vm, a[0], 1);
}

/* dirs(d) -> Error<Directory[]> */
static Value d_dirs(VM* vm, int argc, Value* a) {
    return list_dir(vm, a[0], 2);
}

/* ----------------------------------------------------------- InputStream
 *
 * Reading in pieces, as in Java: the program owns a fixed buffer (an
 * Int8Array, whose length never changes) and read() fills it again and again,
 * returning how many bytes came in, or -1 at the end of the file. The C side
 * freads straight into the typed array's storage, without a copy.
 */

/* checks offset / length against a buffer of len elements */
static void check_span(VM* vm, const char* fn, int64_t off, int64_t n, int64_t len) {
    if (off < 0 || n < 0 || off > len - n) {
        rt_error(vm, 0, "%s(buffer, %" PRId64 ", %" PRId64 ") is out of range for a buffer of length %" PRId64, fn, off, n, len);
    }
}

/* read(in) -> int: the next byte as 0...=255, -1 at the end (like Java's InputStream.read) */
static Value in_read_byte(VM* vm, int argc, Value* a) {
    FILE* fp = open_stream(vm, a[0]);
    const int c = fgetc(fp);
    if (c == EOF && ferror(fp)) {
        rt_error(vm, 0, "cannot read '%s': %s", STREAM(a[0])->path, strerror(errno));
    }

    return int_val(c == EOF ? -1 : c);
}

/*
 * read(in, buffer), read(in, buffer, offset, length) -> int: fills the buffer
 * (or length bytes of it from offset), the number of bytes read, -1 at the end
 */
static Value in_read(VM* vm, int argc, Value* a) {
    FILE* fp = open_stream(vm, a[0]);
    const ObjTypedArray* buf = (ObjTypedArray*)a[1].as.o;
    int64_t off = 0, n = buf->length;
    if (argc > 2) {
        off = a[2].as.i;
        n = a[3].as.i;
        check_span(vm, "read", off, n, buf->length);
    }

    if (n == 0) {
        return int_val(0);
    }

    const size_t got = fread((char*)buf->data + off, 1, (size_t)n, fp);
    if (got == 0 && ferror(fp)) {
        rt_error(vm, 0, "cannot read '%s': %s", STREAM(a[0])->path, strerror(errno));
    }

    return int_val(got == 0 ? -1 : (int64_t)got);
}

/* the most bytes read_text(in) reads at once: a string holds at most INT32_MAX bytes (2 GB) */
#define READ_TEXT_MAX ((int64_t)INT32_MAX)

/*
 * read_text(in), read_text(in, length) -> string: the rest of the stream, or
 * up to length bytes of it, as text; "" at the end. Without a length it reads
 * up to READ_TEXT_MAX bytes. length counts bytes, so it can end in the middle
 * of a UTF-8 character.
 */
static Value in_read_text(VM* vm, int argc, Value* a) {
    FILE* fp = open_stream(vm, a[0]);
    const char* path = STREAM(a[0])->path;
    const int64_t length = argc > 1 ? a[1].as.i : READ_TEXT_MAX;
    if (length < 0 || length > READ_TEXT_MAX) {
        rt_error(vm, 0, "read_text(stream, %" PRId64 "): the length must be 0...=%" PRId64, length, READ_TEXT_MAX);
    }

    StrBuf sb;
    sb_init(&sb);

    char chunk[4096];
    int64_t remaining = length;
    while (remaining > 0) {
        const size_t want = remaining < (int64_t)sizeof chunk ? (size_t)remaining : sizeof chunk;
        const size_t got = fread(chunk, 1, want, fp);
        sb_append(&sb, chunk, got);
        remaining -= (int64_t)got;

        if (got < want) {
            if (ferror(fp)) {
                const int err = errno;
                sb_free(&sb);
                rt_error(vm, 0, "cannot read '%s': %s", path, strerror(err));
            }

            break;
        }
    }

    const Value s = obj_val(str_new(vm, sb.data, (int)sb.len));
    sb_free(&sb);

    return s;
}

/* skip(in, n) -> int: skips up to n bytes, the number skipped */
static Value in_skip(VM* vm, int argc, Value* a) {
    FILE* fp = open_stream(vm, a[0]);
    const int64_t n = a[1].as.i;
    int64_t done = 0;
    char tmp[4096];
    while (done < n) {
        const size_t want = (size_t)(n - done < (int64_t)sizeof tmp ? n - done : (int64_t)sizeof tmp);
        const size_t got = fread(tmp, 1, want, fp);
        done += (int64_t)got;
        if (got < want) {
            break;
        }
    }

    return int_val(done);
}

/* ---------------------------------------------------------- OutputStream
 *
 * Writes go into the C library's buffer and reach the file when it fills, on
 * flush(), on close(), or at the latest when the program ends.
 */

static void out_put(VM* vm, Value s, const void* data, size_t n) {
    FILE* fp = open_stream(vm, s);
    if (fp == stderr) {
        /* like io:eprint: what was printed before comes first */
        fflush(stdout);
    }

    if (fwrite(data, 1, n, fp) != n) {
        rt_error(vm, 0, "cannot write '%s': %s", STREAM(s)->path, strerror(errno));
    }
}

/* write(out, byte): one byte, -128...=255 (an Int8Array element or 0...=255) */
static Value out_write_byte(VM* vm, int argc, Value* a) {
    const int64_t b = a[1].as.i;
    if (b < -128 || b > 255) {
        rt_error(vm, 0, "write(out, %" PRId64 "): a byte is -128...=255", b);
    }

    const unsigned char c = (unsigned char)b;
    out_put(vm, a[0], &c, 1);

    return void_val();
}

/* write(out, data): an Int8Array or a string (its UTF-8 bytes); write(out, buffer, offset, length) */
static Value out_write(VM* vm, int argc, Value* a) {
    size_t len;
    const char* data = bytes_of(vm, a[1], &len);
    if (argc > 2) {
        check_span(vm, "write", a[2].as.i, a[3].as.i, (int64_t)len);
        data += a[2].as.i;
        len = (size_t)a[3].as.i;
    }

    out_put(vm, a[0], data, len);

    return void_val();
}

static Value out_flush(VM* vm, int argc, Value* a) {
    if (fflush(open_stream(vm, a[0])) != 0) {
        rt_error(vm, 0, "cannot write '%s': %s", STREAM(a[0])->path, strerror(errno));
    }

    return void_val();
}

/* ------------------------------------------------------ both streams */

/*
 * close(s): closes the FILE* now and marks the stream closed, so the GC's
 * stream_free has nothing left to do. Closing twice does nothing, and
 * fclose's error (data that could not be flushed) is reported here.
 * fs:stdin, fs:stdout and fs:stderr are only flushed and marked closed.
 */
static Value s_close(VM* vm, int argc, Value* a) {
    ObjStream* s = STREAM(a[0]);
    if (s->fp && s->is_std) {
        /* the process keeps its standard streams: io:print and io:input still use them */
        if (s->fp != stdin) {
            fflush(s->fp);
        }

        s->fp = NULL;
    } else if (s->fp) {
        const int r = fclose(s->fp);
        s->fp = NULL;
        if (r != 0) {
            rt_error(vm, 0, "cannot close '%s': %s", s->path, strerror(errno));
        }
    }

    return void_val();
}

static Value s_is_open(VM* vm, int argc, Value* a) {
    return bool_val(STREAM(a[0])->fp != NULL);
}

static Value s_path(VM* vm, int argc, Value* a) {
    return obj_val(str_cnew(vm, STREAM(a[0])->path));
}

/* ---------------------------------------------------------- registration
 *
 * TYPES: one NativeClass per `native type` in fs.pp. `constructor` is only
 * used in the message for fs:File(...) ("make a value with fs:file(...)").
 * Hooks left out get defaults: no free, <Name> printing, identity ==, and
 * copy(x) returning x. Classes without length / get / set cannot be indexed,
 * measured with # or iterated.
 *
 * FS: the C body of every `native function` in fs.pp, by the name written
 * after `native` ("f_read") or the function's own name (read_text). A name
 * fs.pp declares but this table lacks stops the module from loading; an entry
 * nothing declares is simply unused.
 */

#define PATH_CLASS(n, ctor)                                                                                   \
    {.name = n, .size = sizeof(ObjPath), .constructor = ctor, .free = path_free, .format = path_format, \
     .equals = path_equals}
#define STREAM_CLASS(n, ctor) \
    {.name = n, .size = sizeof(ObjStream), .constructor = ctor, .free = stream_free, .format = stream_format}

static const NativeClass TYPES[] = {
    PATH_CLASS("File", "fs:file"),
    PATH_CLASS("Directory", "fs:directory"),
    STREAM_CLASS("InputStream", "file.open_read"),
    STREAM_CLASS("OutputStream", "file.open_write"),
    {.name = NULL},
};

static const NativeImpl FS[] = {
    {"name_separator", fs_name_separator},
    {"list_separator", fs_list_separator},
    {"exists", fs_exists},
    {"is_dir", fs_is_dir},
    {"is_file", fs_is_file},
    {"cwd", fs_cwd},
    {"file", fs_file},
    {"directory", fs_directory},

    {"p_path", p_path},
    {"p_name", p_name},
    {"p_parent", p_parent},
    {"p_exists", p_exists},
    {"p_absolute", p_absolute},
    {"p_rename", p_rename},
    {"p_modified", p_modified},

    {"f_is_file", f_is_file},
    {"extension", f_extension},
    {"size", f_size},
    {"f_read", f_read},
    {"read_text", f_read_text},
    {"f_write", f_write},
    {"append", f_append},
    {"f_create", f_create},
    {"f_delete", f_delete},
    {"copy_to", f_copy_to},
    {"open_read", f_open_read},
    {"open_write", f_open_write},
    {"open_append", f_open_append},

    {"d_is_dir", d_is_dir},
    {"d_create", d_create},
    {"create_all", d_create_all},
    {"d_delete", d_delete},
    {"list", d_list},
    {"files", d_files},
    {"dirs", d_dirs},

    {"in_stdin", in_stdin},
    {"in_read_byte", in_read_byte},
    {"in_read", in_read},
    {"in_read_text", in_read_text},
    {"skip", in_skip},
    {"out_stdout", out_stdout},
    {"out_stderr", out_stderr},
    {"out_write_byte", out_write_byte},
    {"out_write", out_write},
    {"flush", out_flush},
    {"s_close", s_close},
    {"s_is_open", s_is_open},
    {"s_path", s_path},
    {NULL, NULL},
};

/*
 * Called once from vm_new (src/interp.c) and declared in vm.h. "fs" is the
 * library module name: the tables apply to lib/fs.pp, found by `import fs`.
 */
void mod_fs_register(VM* vm) {
    vm_register_native_types(vm, "fs", TYPES);
    vm_register_native_impls(vm, "fs", FS);
}
