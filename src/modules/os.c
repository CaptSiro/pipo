/*
 * The `os` module and its `os:fs` submodule:
 *
 *     import os
 *     io:print(os:platform, os:args())
 *     if os:fs:is_dir("lib") { ... }
 *
 *     import os:fs                // just the submodule, bound as `fs`
 *     import os:fs -> { exists }
 *
 * Kept deliberately small; it is also the example of a native submodule.
 */
#include "vm.h"

#ifdef _WIN32
#define PP_PLATFORM "windows"
#elif defined(__APPLE__)
#define PP_PLATFORM "macos"
#else
#define PP_PLATFORM "linux"
#endif

/* args() -> string[]: the script path and everything after it */
static Value os_args(VM* vm, int argc, Value* a) {
    ObjArray* arr = array_new(vm, vm->argc);
    vm_push(vm, obj_val(arr));
    for (int i = 1; i < vm->argc; i++) {
        array_push(arr, obj_val(str_cnew(vm, vm->argv[i])));
    }

    vm->sp--;

    return obj_val(arr);
}

#ifdef _WIN32
#define PP_SEP '\\'
#else
#define PP_SEP '/'
#endif

static bool is_sep(char c) {
    return c == '/' || c == '\\';
}

static bool path_is_absolute(const char* p) {
    if (is_sep(p[0])) {
        return true;
    }

    return ((p[0] >= 'a' && p[0] <= 'z') || (p[0] >= 'A' && p[0] <= 'Z')) && p[1] == ':';
}

/*
 * Appends one path part: exactly one separator between parts, an absolute
 * part starts over (like Python's os.path.join), an empty part is skipped.
 */
static void join_part(StrBuf* sb, const char* part) {
    if (!part[0]) {
        return;
    }

    if (path_is_absolute(part)) {
        sb->len = 0;
    } else if (sb->len && !is_sep(sb->data[sb->len - 1])) {
        sb_putc(sb, PP_SEP);
    }

    if (sb->len && is_sep(sb->data[sb->len - 1])) {
        while (is_sep(*part)) {
            part++;
        }
    }

    sb_puts(sb, part);
}

static Value join_args(VM* vm, const char* fname, StrBuf* sb, int argc, Value* a) {
    for (int i = 0; i < argc; i++) {
        if (!IS_KIND(a[i], OBJ_STRING)) {
            rt_error(vm, 0, "os:%s expects strings, but argument %d is %s", fname, i + 1, value_type_name(a[i]));
        }

        join_part(sb, AS_STR(a[i])->chars);
    }

    const Value v = obj_val(str_new(vm, sb->data ? sb->data : "", (int)sb->len));
    sb_free(sb);

    return v;
}

/* join(parts...) -> string: os:join("data", "order.txt") == "data\order.txt" on Windows */
static Value os_join(VM* vm, int argc, Value* a) {
    StrBuf sb;
    sb_init(&sb);

    return join_args(vm, "join", &sb, argc, a);
}

/* join_pp(parts...) -> string: os:join(pp:dir(), parts...), relative to the file the call is written in */
static Value os_join_pp(VM* vm, int argc, Value* a) {
    StrBuf sb;
    sb_init(&sb);
    char* dir = vm_script_dir(vm);
    sb_puts(&sb, dir);
    free(dir);

    return join_args(vm, "join_pp", &sb, argc, a);
}

static ObjModule* os_init(VM* vm) {
    ObjModule* m = module_new(vm, "os", "<native os>", vm->builtins);
    vm_push(vm, obj_val(m));
    def_const_str(vm, m->env, "platform", PP_PLATFORM);
    def_native(vm, m->env, "args", "() -> string[]", os_args);
    def_native(vm, m->env, "join", "(...) -> string", os_join);
    def_native(vm, m->env, "join_pp", "(...) -> string", os_join_pp);
    module_export_all(m);
    vm->sp--;

    return m;
}

void mod_os_register(VM* vm) {
    vm_register_native_module(vm, "os", os_init);
}
