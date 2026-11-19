// Functions of a DLL / shared library, called straight from PiPo (md/EXTERN.md):
//
//     import c
//     extern "glfw3" function glfwInit() -> c:i32
//     extern "glfw3" "glfwCreateWindow" function create_window(c:i32 w, c:i32 h, string title, c:ptr monitor,
//                                                              c:ptr share) -> c:ptr
//
// The declaration gives the PiPo types; the C types follow from them (the `c` module's aliases
// name the C widths: c:u32 is an int passed as a 32-bit unsigned). The library is loaded and the
// symbol looked up on the first call, so declaring functions of a library that is missing costs
// nothing until one of them runs.
//
// There is no libffi: a call goes through a function pointer cast to a prototype the C compiler
// knows. Every argument is first turned into a 64-bit slot; the prototype only decides whether a
// slot travels in an integer or in a floating point register.
//
//   * Windows x64: the first four arguments go in RCX/RDX/R8/R9 or XMM0-3 by position, the rest
//     on the stack in 8-byte slots. So 16 prototypes cover the first four (each integer or
//     double), the rest are integer slots holding the raw bits. A float (f32) travels as a double
//     whose low 32 bits are the float, which is what the callee reads.
//   * System V x86-64 (Linux, macOS on Intel): integers fill RDI..R9 in order, floats XMM0-7 in
//     order, independently; whatever does not fit goes on the stack in order. One prototype with
//     6 integer, 8 double and 10 stack slots covers every mix.
//
// The caller cleans the stack on both ABIs, so passing more slots than the callee reads is
// harmless. Not supported: structs by value, variadic C functions, callbacks (C calling PiPo).
#include <math.h>
#include <string.h>

#include "vm.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <dlfcn.h>
#endif



#define EXTERN_MAX_ARGS 16

typedef enum {
    C_VOID,
    C_I8,
    C_U8,
    C_I16,
    C_U16,
    C_I32,
    C_U32,
    C_I64,
    C_U64,
    C_BOOL, // passed as an int 0 / 1, returned as a C int (non-zero is true)
    C_F32,
    C_F64,
    C_PTR,  // an address held in an int (c:ptr), 0 is NULL
    C_STR,  // string: const char*, NUL-terminated
    C_STRS, // string[]: const char**, built for the call
    C_BUF   // a types:*Array: a pointer to its elements, which C may change
} CType;

typedef struct {
    CType type;
    bool nullable; // T?: none() is NULL
} CParam;

struct Extern {
    char* name;   // the PiPo function, for messages
    char* lib;    // as written: "glfw3", "./bin/glfw3.dll"
    char* symbol; // the C name
    char* dir;    // directory of the declaring file, searched first; NULL for none
    CParam params[EXTERN_MAX_ARGS];
    int nparams;
    CType ret;
    void* handle;
    void (*fp)(void);
};

typedef uint64_t U;
typedef double D;



// ---------------------------------------------------------------- the C types of a signature

static const struct {
    const char* name;
    CType type;
} C_NAMES[] = {
    {"i8", C_I8},   {"u8", C_U8},   {"i16", C_I16}, {"u16", C_U16}, {"i32", C_I32}, {"u32", C_U32},
    {"i64", C_I64}, {"u64", C_U64}, {"f32", C_F32}, {"f64", C_F64}, {"ptr", C_PTR},
};

// a types:*Array, or one of c.pp's pointers over one (c:Int32Pointer ...): a pointer to its elements
static bool is_typed_array_name(const char* s) {
    static const char* const NAMES[] = {"Int8Array",    "Int16Array",    "Int32Array",    "Int64Array",
                                        "Float32Array", "Float64Array",  "Int32Pointer",  "Uint32Pointer",
                                        "Int64Pointer"};
    for (size_t i = 0; i < sizeof NAMES / sizeof NAMES[0]; ++i) {
        if (strcmp(s, NAMES[i]) == 0) {
            return true;
        }
    }

    return false;
}

static bool set_type(CParam* out, CType type) {
    out->type = type;

    return true;
}

// the C type of a parameter or return type as written; false when C has no counterpart
static bool ctype_of(const Type* t, bool is_ret, CParam* out) {
    out->nullable = false;
    if (!t) {
        return false;
    }

    if (t->kind == TY_PRIM) {
        switch (t->prim) {
            case PRIM_INT: return set_type(out, C_I64);
            case PRIM_FLOAT: return set_type(out, C_F64);
            case PRIM_BOOL: return set_type(out, C_BOOL);
            case PRIM_STRING: return set_type(out, C_STR);
            case PRIM_VOID: return is_ret && set_type(out, C_VOID);
            default: return false;
        }
    }

    if (t->kind == TY_ARRAY) {
        out->type = C_STRS;

        return !is_ret && t->inner && t->inner->kind == TY_PRIM && t->inner->prim == PRIM_STRING;
    }

    if (t->kind != TY_NAME) {
        return false;
    }

    const char* name = t->name->chars;
    if (strcmp(name, "Option") == 0 && t->nargs == 1 && !is_ret) {
        // string? and types:Float32Array?: none() is NULL
        if (!ctype_of(t->args[0], false, out) || (out->type != C_STR && out->type != C_BUF)) {
            return false;
        }

        out->nullable = true;

        return true;
    }

    if (is_typed_array_name(name)) {
        out->type = C_BUF;

        return !is_ret;
    }

    for (size_t i = 0; i < sizeof C_NAMES / sizeof C_NAMES[0]; ++i) {
        if (strcmp(name, C_NAMES[i].name) == 0) {
            out->type = C_NAMES[i].type;

            return true;
        }
    }

    return false;
}

static char* dir_of(const char* file) {
    if (!file || file[0] == '<') {
        return NULL;
    }

    const char* end = NULL;
    for (const char* s = file; *s; ++s) {
        if (*s == '/' || *s == '\\') {
            end = s;
        }
    }

    if (!end) {
        return NULL;
    }

    char* dir = pp_malloc((size_t)(end - file) + 1);
    memcpy(dir, file, (size_t)(end - file));
    dir[end - file] = '\0';

    return dir;
}

Extern* extern_new(Signature* sig, const char* lib, const char* symbol, char* err, size_t errn) {
    const char* fname = sig->name->chars;
    if (sig->ngenerics > 0) {
        snprintf(err, errn, "extern function '%s' cannot be generic: C has one type per parameter", fname);

        return NULL;
    }

    if (sig->variadic) {
        snprintf(err, errn, "extern function '%s' cannot be variadic", fname);

        return NULL;
    }

    if (sig->nparams > EXTERN_MAX_ARGS) {
        snprintf(err, errn, "extern function '%s' has %d parameters; at most %d are supported", fname, sig->nparams,
                 EXTERN_MAX_ARGS);

        return NULL;
    }

    Extern* e = pp_calloc(1, sizeof(Extern));
    for (int i = 0; i < sig->nparams; ++i) {
        if (!ctype_of(sig->params[i].type, false, &e->params[i])) {
            snprintf(err, errn,
                     "parameter '%s' of extern function '%s' has a type C has no counterpart for\n"
                     "  (use int, float, bool, string, string[], a c: type such as c:i32 / c:f32 / c:ptr, a "
                     "types:*Array, or string? / types:*Array? for a pointer that may be NULL)",
                     sig->params[i].name ? sig->params[i].name->chars : "?", fname);
            free(e);

            return NULL;
        }
    }

    CParam ret;
    if (!ctype_of(sig->ret, true, &ret)) {
        snprintf(err, errn,
                 "extern function '%s' returns a type C has no counterpart for\n"
                 "  (use void, int, float, bool, string or a c: type such as c:i32 / c:u8 / c:f32 / c:ptr)",
                 fname);
        free(e);

        return NULL;
    }

    e->nparams = sig->nparams;
    e->ret = ret.type;
    e->name = pp_strdup(fname);
    e->lib = pp_strdup(lib);
    e->symbol = pp_strdup(symbol);
    e->dir = dir_of(sig->file);

    return e;
}



// ---------------------------------------------------------------- loading

static bool has_dir_part(const char* s) {
    return strchr(s, '/') || strchr(s, '\\');
}

static bool is_absolute(const char* s) {
    return s[0] == '/' || s[0] == '\\' || (s[0] && s[1] == ':');
}

#ifdef _WIN32

static void* lib_open(const char* path) {
    // the DLL's own folder is searched for the DLLs it needs, so glfw3.dll can sit next to the script
    return (void*)LoadLibraryExA(path, NULL, has_dir_part(path) ? LOAD_WITH_ALTERED_SEARCH_PATH : 0);
}

static void (*lib_symbol(void* handle, const char* name))(void) {
    void (*fp)(void) = (void (*)(void))(void*)GetProcAddress((HMODULE)handle, name);
    if (fp) {
        return fp;
    }

    // OpenGL past 1.1 is not exported by opengl32.dll: the driver hands those functions out through
    // wglGetProcAddress, once a context is current (see md/EXTERN.md, "OpenGL")
    typedef void (*(*WglGet)(const char*))(void);
    WglGet wgl = (WglGet)(void*)GetProcAddress((HMODULE)handle, "wglGetProcAddress");
    if (!wgl) {
        return NULL;
    }

    fp = wgl(name);
    const intptr_t v = (intptr_t)fp;
    if (v == 0 || v == 1 || v == 2 || v == 3 || v == -1) {
        return NULL; // some drivers answer with these instead of NULL
    }

    return fp;
}

static void lib_error(char* buf, size_t n) {
    const DWORD code = GetLastError();
    const DWORD len = FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, NULL, code, 0, buf,
                                     (DWORD)n, NULL);
    if (len == 0) {
        snprintf(buf, n, "error %lu", (unsigned long)code);
    }

    size_t k = strlen(buf);
    while (k > 0 && (buf[k - 1] == '\n' || buf[k - 1] == '\r' || buf[k - 1] == '.')) {
        buf[--k] = '\0';
    }
}

#else

static void* lib_open(const char* path) {
    return dlopen(path, RTLD_NOW | RTLD_GLOBAL);
}

static void (*lib_symbol(void* handle, const char* name))(void) {
    void* p = dlsym(handle, name);
    void (*fp)(void);
    memcpy(&fp, &p, sizeof fp);

    return fp;
}

static void lib_error(char* buf, size_t n) {
    const char* e = dlerror();
    snprintf(buf, n, "%s", e ? e : "unknown error");
}

#endif

static bool file_exists(const char* path) {
    FILE* f = fopen(path, "rb");
    if (!f) {
        return false;
    }

    fclose(f);

    return true;
}

// "glfw3": <dir>/glfw3.dll, then the system's search (LoadLibrary adds .dll itself).
// On other systems "glfw" also tries libglfw.so. A path with a folder in it is relative to <dir>.
static void* load_library(VM* vm, Extern* e) {
    const char* lib = e->lib;
    char path[1024];
    char err[512] = "";
    void* h = NULL;
#ifdef _WIN32
    const char* ext = strchr(lib, '.') && !has_dir_part(strrchr(lib, '.')) ? "" : ".dll";
    const char* prefixes[] = {""};
#else
    const char* ext = strchr(lib, '.') && !has_dir_part(strrchr(lib, '.')) ? "" : ".so";
    const char* prefixes[] = {"", "lib"};
#endif
    const int nprefixes = (int)(sizeof prefixes / sizeof prefixes[0]);
    for (int i = 0; i < nprefixes && !h; ++i) {
        if (e->dir && !is_absolute(lib)) {
            snprintf(path, sizeof path, "%s/%s%s%s", e->dir, prefixes[i], lib, ext);
            if (file_exists(path)) {
                h = lib_open(path);
                if (!h) {
                    lib_error(err, sizeof err);
                }
            }
        }

        if (!h && !has_dir_part(lib)) {
            snprintf(path, sizeof path, "%s%s%s", prefixes[i], lib, ext);
            h = lib_open(path);
            if (!h && !err[0]) {
                lib_error(err, sizeof err);
            }
        }

        if (!h && is_absolute(lib)) {
            h = lib_open(lib);
            if (!h && !err[0]) {
                lib_error(err, sizeof err);
            }
        }
    }

    if (!h) {
        rt_error(vm, 0, "cannot load library '%s' for extern function '%s': %s\n  (looked in %s%s)", lib, e->name,
                 err[0] ? err : "not found", e->dir ? e->dir : "", e->dir ? " and the system's library path" :
                                                                            "the system's library path");
    }

    return h;
}

static void resolve(VM* vm, Extern* e) {
    if (!e->handle) {
        e->handle = load_library(vm, e);
    }

    e->fp = lib_symbol(e->handle, e->symbol);
    if (!e->fp) {
        rt_error(vm, 0, "library '%s' has no function '%s'%s", e->lib, e->symbol,
                 strncmp(e->symbol, "gl", 2) == 0 && e->symbol[2] >= 'A' && e->symbol[2] <= 'Z'
                     ? "\n  (OpenGL functions past version 1.1 exist only once a context is current: make one current "
                       "before the first call)"
                     : "");
    }
}



// ---------------------------------------------------------------- calling

static U f32_slot(double x) {
    const float f = (float)x;
    uint32_t bits;
    memcpy(&bits, &f, sizeof bits);

    return (U)bits;
}

static U f64_slot(double x) {
    U bits;
    memcpy(&bits, &x, sizeof bits);

    return bits;
}

static double num_of(Value v) {
    return v.kind == VAL_INT ? (double)v.as.i : v.as.f;
}

// what the narrow int types take, in CType order from C_I8
typedef struct {
    int64_t lo, hi;
    const char* cname;
} IntRange;

static const IntRange INT_RANGES[] = {
    {INT8_MIN, INT8_MAX, "int8_t"},   {0, UINT8_MAX, "uint8_t"},   {INT16_MIN, INT16_MAX, "int16_t"},
    {0, UINT16_MAX, "uint16_t"},      {INT32_MIN, INT32_MAX, "int32_t"}, {0, UINT32_MAX, "uint32_t"},
};

// the payload of some(x), or false for none()
static bool unwrap_option(VM* vm, Value v, Value* out) {
    const ObjEnumValue* ev = (const ObjEnumValue*)v.as.o;
    if (ev->en == vm->option_enum && strcmp(ev->en->variants[ev->variant].name->chars, "some") == 0) {
        *out = ev->payload[0];

        return true;
    }

    return false;
}

// one argument as a 64-bit slot; string[] arrays are allocated into *tmp, freed after the call
static U slot_of(VM* vm, Extern* e, int i, Value v, void** tmp) {
    const CParam* p = &e->params[i];
    if (p->nullable && !unwrap_option(vm, v, &v)) {
        return 0;
    }

    switch (p->type) {
        case C_I8:
        case C_U8:
        case C_I16:
        case C_U16:
        case C_I32:
        case C_U32: {
            const IntRange* r = &INT_RANGES[p->type - C_I8];
            if (v.as.i < r->lo || v.as.i > r->hi) {
                rt_error(vm, 0, "argument %d of '%s' is %" PRId64 ", which does not fit in a C %s", i + 1, e->name,
                         v.as.i, r->cname);
            }

            return (U)v.as.i;
        }
        case C_I64:
        case C_U64:
        case C_PTR: return (U)v.as.i;
        case C_BOOL: return v.as.b ? 1 : 0;
        case C_F32: return f32_slot(num_of(v));
        case C_F64: return f64_slot(num_of(v));
        case C_STR: return (U)(uintptr_t)AS_STR(v)->chars;
        case C_BUF: return (U)(uintptr_t)((ObjTypedArray*)v.as.o)->data;
        case C_STRS: {
            const ObjArray* a = AS_ARRAY(v);
            const char** list = pp_malloc(sizeof(char*) * (size_t)(a->count + 1));
            for (int k = 0; k < a->count; ++k) {
                list[k] = AS_STR(a->items[k])->chars;
            }

            list[a->count] = NULL; // like argv, in case the callee looks for the end
            *tmp = (void*)list;

            return (U)(uintptr_t)list;
        }
        case C_VOID: break;
    }

    return 0;
}

static bool is_float_slot(CType t) {
    return t == C_F32 || t == C_F64;
}

#if defined(_WIN64) && (defined(__x86_64__) || defined(_M_X64))

#define X_REST U, U, U, U, U, U, U, U, U, U, U, U
#define X_REST_V u[4], u[5], u[6], u[7], u[8], u[9], u[10], u[11], u[12], u[13], u[14], u[15]
#define X_V_U(I) u[I]
#define X_V_D(I) d[I]
#define X_CALL(R, A, B, C, E) ((R(*)(A, B, C, E, X_REST))fp)(X_V_##A(0), X_V_##B(1), X_V_##C(2), X_V_##E(3), X_REST_V)
#define X_SWITCH(R, OUT) \
    switch (mask) { \
        case 0: OUT = X_CALL(R, U, U, U, U); break; \
        case 1: OUT = X_CALL(R, D, U, U, U); break; \
        case 2: OUT = X_CALL(R, U, D, U, U); break; \
        case 3: OUT = X_CALL(R, D, D, U, U); break; \
        case 4: OUT = X_CALL(R, U, U, D, U); break; \
        case 5: OUT = X_CALL(R, D, U, D, U); break; \
        case 6: OUT = X_CALL(R, U, D, D, U); break; \
        case 7: OUT = X_CALL(R, D, D, D, U); break; \
        case 8: OUT = X_CALL(R, U, U, U, D); break; \
        case 9: OUT = X_CALL(R, D, U, U, D); break; \
        case 10: OUT = X_CALL(R, U, D, U, D); break; \
        case 11: OUT = X_CALL(R, D, D, U, D); break; \
        case 12: OUT = X_CALL(R, U, U, D, D); break; \
        case 13: OUT = X_CALL(R, D, U, D, D); break; \
        case 14: OUT = X_CALL(R, U, D, D, D); break; \
        default: OUT = X_CALL(R, D, D, D, D); break; \
    }

// the raw result: integer register, or the bits of XMM0
static U raw_call(VM* vm, Extern* e, const U* slots) {
    void (*fp)(void) = e->fp;
    U u[EXTERN_MAX_ARGS] = {0};
    D d[4] = {0};
    int mask = 0;
    for (int i = 0; i < e->nparams; ++i) {
        u[i] = slots[i];
        if (i < 4 && is_float_slot(e->params[i].type)) {
            mask |= 1 << i;
            memcpy(&d[i], &slots[i], sizeof(D));
        }
    }

    if (is_float_slot(e->ret)) {
        D r = 0;
        X_SWITCH(D, r)
        U bits;
        memcpy(&bits, &r, sizeof bits);

        return bits;
    }

    U r = 0;
    X_SWITCH(U, r)

    return r;
}

#elif defined(__x86_64__) && !defined(_WIN32)

#define S_PROTO U, U, U, U, U, U, D, D, D, D, D, D, D, D, U, U, U, U, U, U, U, U, U, U
#define S_ARGS \
    gi[0], gi[1], gi[2], gi[3], gi[4], gi[5], gd[0], gd[1], gd[2], gd[3], gd[4], gd[5], gd[6], gd[7], st[0], st[1], \
        st[2], st[3], st[4], st[5], st[6], st[7], st[8], st[9]

static U raw_call(VM* vm, Extern* e, const U* slots) {
    void (*fp)(void) = e->fp;
    U gi[6] = {0};
    D gd[8] = {0};
    U st[10] = {0};
    int ni = 0, nd = 0, ns = 0;
    for (int i = 0; i < e->nparams; ++i) {
        if (is_float_slot(e->params[i].type) && nd < 8) {
            memcpy(&gd[nd++], &slots[i], sizeof(D));
        } else if (!is_float_slot(e->params[i].type) && ni < 6) {
            gi[ni++] = slots[i];
        } else if (ns < 10) {
            st[ns++] = slots[i];
        } else {
            rt_error(vm, 0, "extern function '%s' passes too many arguments on the stack", e->name);
        }
    }

    if (is_float_slot(e->ret)) {
        const D r = ((D(*)(S_PROTO))fp)(S_ARGS);
        U bits;
        memcpy(&bits, &r, sizeof bits);

        return bits;
    }

    return ((U(*)(S_PROTO))fp)(S_ARGS);
}

#else

static U raw_call(VM* vm, Extern* e, const U* slots) {
    rt_error(vm, 0, "extern functions are only supported on x86-64 (Windows and System V) for now");
}

#endif

Value extern_call(VM* vm, Extern* e, int argc, Value* args) {
    if (!e->fp) {
        resolve(vm, e);
    }

    U slots[EXTERN_MAX_ARGS];
    void* tmp[EXTERN_MAX_ARGS] = {0};
    for (int i = 0; i < e->nparams && i < argc; ++i) {
        slots[i] = slot_of(vm, e, i, args[i], &tmp[i]);
    }

    const U r = raw_call(vm, e, slots);
    for (int i = 0; i < e->nparams; ++i) {
        free(tmp[i]);
    }

    switch (e->ret) {
        case C_VOID: return void_val();
        case C_I8: return int_val((int8_t)r);
        case C_U8: return int_val((uint8_t)r);
        case C_I16: return int_val((int16_t)r);
        case C_U16: return int_val((uint16_t)r);
        case C_I32: return int_val((int32_t)r);
        case C_U32: return int_val((uint32_t)r);
        case C_I64:
        case C_U64:
        case C_PTR: return int_val((int64_t)r);
        case C_BOOL: return bool_val((int32_t)r != 0);
        case C_F32: {
            const uint32_t bits = (uint32_t)r;
            float f;
            memcpy(&f, &bits, sizeof f);

            return float_val((double)f);
        }
        case C_F64: {
            double f;
            memcpy(&f, &r, sizeof f);

            return float_val(f);
        }
        case C_STR: {
            const char* s = (const char*)(uintptr_t)r;

            return obj_val(str_cnew(vm, s ? s : ""));
        }
        default: break;
    }

    return void_val();
}

// the NativeFn every extern ObjNative / BcNative carries; the call sites pass its Extern instead
Value extern_native(VM* vm, int argc, Value* args) {
    rt_error(vm, 0, "internal: an extern function was called without its declaration");
}
