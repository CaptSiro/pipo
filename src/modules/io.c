/*
 * The `io` module:
 *
 *     import io
 *     io:print("hello", 42)
 *     io:write("no newline" + io:nl)
 *
 * It is a native module: its functions are written in C, and the module is
 * created the first time something imports it.
 */
#include "vm.h"

static Value format_args(VM* vm, StrBuf* sb, int argc, Value* a) {
    for (int i = 0; i < argc; i++) {
        if (i) {
            sb_putc(sb, ' ');
        }

        value_format(sb, a[i], true, 0);
    }

    return void_val();
}

static Value io_print(VM* vm, int argc, Value* a) {
    StrBuf sb;
    sb_init(&sb);
    format_args(vm, &sb, argc, a);
    fwrite(sb.data, 1, sb.len, stdout);
    sb_free(&sb);

    return void_val();
}

static Value io_print_nl(VM* vm, int argc, Value* a) {
    StrBuf sb;
    sb_init(&sb);
    format_args(vm, &sb, argc, a);
    sb_putc(&sb, '\n');
    fwrite(sb.data, 1, sb.len, stdout);
    sb_free(&sb);

    return void_val();
}

static Value io_eprint(VM* vm, int argc, Value* a) {
    StrBuf sb;
    sb_init(&sb);
    format_args(vm, &sb, argc, a);
    fflush(stdout);
    fwrite(sb.data, 1, sb.len, stderr);
    fflush(stderr);
    sb_free(&sb);

    return void_val();
}

static Value io_eprint_nl(VM* vm, int argc, Value* a) {
    StrBuf sb;
    sb_init(&sb);
    format_args(vm, &sb, argc, a);
    sb_putc(&sb, '\n');
    fflush(stdout);
    fwrite(sb.data, 1, sb.len, stderr);
    fflush(stderr);
    sb_free(&sb);

    return void_val();
}

static Value io_input(VM* vm, int argc, Value* a) {
    if (argc > 0) {
        io_print(vm, argc, a);
    }

    fflush(stdout);
    StrBuf sb;
    sb_init(&sb);
    int c;
    while ((c = fgetc(stdin)) != EOF && c != '\n') {
        sb_putc(&sb, (char)c);
    }

    if (sb.len > 0 && sb.data[sb.len - 1] == '\r') {
        sb.len--;
    }

    const Value r = obj_val(str_new(vm, sb.data, (int)sb.len));
    sb_free(&sb);

    return r;
}

static Value fail_errno(VM* vm, const char* what, const char* path) {
    StrBuf sb;
    sb_init(&sb);
    sb_printf(&sb, "cannot %s '%s'", what, path);
    const Value r = make_fail(vm, obj_val(str_new(vm, sb.data, (int)sb.len)));
    sb_free(&sb);

    return r;
}

/* read_file(path) -> Error<string> */
static Value io_read_file(VM* vm, int argc, Value* a) {
    const char* path = AS_STR(a[0])->chars;
    size_t len;
    char* data = pp_read_file(path, &len);
    if (!data) {
        return fail_errno(vm, "read", path);
    }

    const Value s = obj_val(str_new(vm, data, (int)len));
    free(data);

    return make_success(vm, s);
}

/* write_file(path, content) -> Error<int> (bytes written) */
static Value io_write_file(VM* vm, int argc, Value* a) {
    const char* path = AS_STR(a[0])->chars;
    const ObjString* content = AS_STR(a[1]);
    FILE* f = fopen(path, "wb");
    if (!f) {
        return fail_errno(vm, "open for writing", path);
    }

    const size_t n = fwrite(content->chars, 1, (size_t)content->len, f);
    const bool ok = n == (size_t)content->len && fclose(f) == 0;
    if (!ok) {
        return fail_errno(vm, "write", path);
    }

    return make_success(vm, int_val((int64_t)n));
}

static ObjModule* io_init(VM* vm) {
    ObjModule* m = module_new(vm, "io", "<native io>", vm->builtins);
    vm_push(vm, obj_val(m));
    def_const_str(vm, m->env, "nl", "\n");
    def_native(vm, m->env, "print", "(...)", io_print);
    def_native(vm, m->env, "print_nl", "(...)", io_print_nl);
    def_native(vm, m->env, "eprint", "(...)", io_eprint);
    def_native(vm, m->env, "eprint_nl", "(...)", io_eprint_nl);
    def_native(vm, m->env, "input", "(...) -> string", io_input);
    def_native(vm, m->env, "read_file", "(string path)", io_read_file);
    def_native(vm, m->env, "write_file", "(string path, string content)", io_write_file);
    module_export_all(m);
    vm->sp--;

    return m;
}

void mod_io_register(VM* vm) {
    vm_register_native_module(vm, "io", io_init);
}
