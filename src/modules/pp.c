/*
 * The `pp` module: facts about the running script, like PHP's __FILE__,
 * __DIR__ and __LINE__.
 *
 *     import pp
 *     io:print(pp:file(), pp:dir(), pp:line())
 *
 * They describe the file the *call is written in*, not the entry script:
 * pp:dir() inside an imported module is that module's directory.
 */
#include <limits.h>
#include <stdlib.h>
#include <string.h>

#include "vm.h"

/* the file the running code was written in, or NULL for the REPL / internal code */
static const char* script_file(VM* vm) {
    const char* f = vm->cur_file;
    return f && f[0] != '<' ? f : NULL;
}

static char* current_dir(void) {
#ifdef _WIN32
    char buf[_MAX_PATH * 4];
    if (_fullpath(buf, ".", sizeof buf)) {
        return pp_strdup(buf);
    }
#else
    char buf[PATH_MAX];
    if (realpath(".", buf)) {
        return pp_strdup(buf);
    }
#endif

    return pp_strdup(".");
}

char* vm_script_dir(VM* vm) {
    const char* f = script_file(vm);
    if (!f) {
        return current_dir();
    }

    const char* slash = NULL;
    for (const char* c = f; *c; c++) {
        if (*c == '/' || *c == '\\') {
            slash = c;
        }
    }

    if (!slash) {
        return current_dir();
    }

    if (slash == f) {
        return pp_strndup(f, 1); /* "/script.pp" -> "/" */
    }
#ifdef _WIN32
    if (slash == f + 2 && f[1] == ':') {
        return pp_strndup(f, 3); /* "C:\script.pp" -> "C:\" */
    }
#endif

    return pp_strndup(f, (size_t)(slash - f));
}

/* file() -> string: absolute path of the file this call is written in */
static Value pp_file(VM* vm, int argc, Value* a) {
    const char* f = script_file(vm);
    return obj_val(str_cnew(vm, f ? f : (vm->cur_file ? vm->cur_file : "<unknown>")));
}

/* dir() -> string: directory of that file (the working directory in the REPL) */
static Value pp_dir(VM* vm, int argc, Value* a) {
    char* d = vm_script_dir(vm);
    const Value v = obj_val(str_cnew(vm, d));
    free(d);

    return v;
}

/* line() -> int: line of this call */
static Value pp_line(VM* vm, int argc, Value* a) {
    return int_val(vm->cur_line);
}

static ObjModule* pp_init(VM* vm) {
    ObjModule* m = module_new(vm, "pp", "<native pp>", vm->builtins);
    vm_push(vm, obj_val(m));
    def_native(vm, m->env, "file", "() -> string", pp_file);
    def_native(vm, m->env, "dir", "() -> string", pp_dir);
    def_native(vm, m->env, "line", "() -> int", pp_line);
    module_export_all(m);
    vm->sp--;

    return m;
}

void mod_pp_register(VM* vm) {
    vm_register_native_module(vm, "pp", pp_init);
}
