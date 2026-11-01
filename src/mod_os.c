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
#include <sys/stat.h>

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
    for (int i = 1; i < vm->argc; i++) array_push(arr, obj_val(str_cnew(vm, vm->argv[i])));
    vm->sp--;
    return obj_val(arr);
}

static Value fs_exists(VM* vm, int argc, Value* a) {
    struct stat st;
    return bool_val(stat(AS_STR(a[0])->chars, &st) == 0);
}

static Value fs_is_dir(VM* vm, int argc, Value* a) {
    struct stat st;
    return bool_val(stat(AS_STR(a[0])->chars, &st) == 0 && (st.st_mode & S_IFMT) == S_IFDIR);
}

static ObjModule* fs_init(VM* vm) {
    ObjModule* m = module_new(vm, "fs", "<native os:fs>", vm->builtins);
    vm_push(vm, obj_val(m));
    def_native(vm, m->env, "exists", "(string path) -> bool", fs_exists);
    def_native(vm, m->env, "is_dir", "(string path) -> bool", fs_is_dir);
    module_export_all(m);
    vm->sp--;
    return m;
}

static ObjModule* os_init(VM* vm) {
    ObjModule* m = module_new(vm, "os", "<native os>", vm->builtins);
    vm_push(vm, obj_val(m));
    def_const_str(vm, m->env, "platform", PP_PLATFORM);
    def_native(vm, m->env, "args", "() -> string[]", os_args);
    def_submodule(vm, m, "fs", fs_init);
    module_export_all(m);
    vm->sp--;
    return m;
}

void mod_os_register(VM* vm) {
    vm_register_native_module(vm, "os", os_init);
}
