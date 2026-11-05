#include "vm.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

static void usage(void) {
    printf("pp " PP_VERSION "\n"
           "usage:\n"
           "  pp <file.pp> [args...]   run a program\n"
           "  pp                       start the interactive REPL\n"
           "  pp --version             print the version\n"
           "  pp --help                show this message\n"
           "\n"
           "modules are searched next to the importing file, in <pp dir>/lib\n"
           "and in the directories listed in the PP_PATH environment variable\n");
}

/* counts unclosed (, [ and { so the REPL knows when to ask for more input */
static int open_depth(const char* s) {
    int depth = 0;
    char quote = 0;
    for (const char* p = s; *p; p++) {
        if (quote) {
            if (*p == '\\' && p[1]) {
                p++;
            } else if (*p == quote) {
                quote = 0;
            }

            continue;
        }

        if (p[0] == '/' && p[1] == '/') {
            while (*p && *p != '\n') {
                p++;
            }

            if (!*p) {
                break;
            }

            continue;
        }

        if (*p == '"' || *p == '\'') {
            quote = *p;
        } else if (*p == '(' || *p == '[' || *p == '{') {
            depth++;
        } else if (*p == ')' || *p == ']' || *p == '}') {
            depth--;
        }
    }

    return depth;
}

static int repl(VM* vm) {
    printf("pp " PP_VERSION " - type 'exit' to quit\n");
    ObjEnv* env = env_new(vm, vm->builtins);
    vm->envs[vm->esp++] = env; /* keeps the REPL scope alive across collections */
    StrBuf input;
    sb_init(&input);
    char line[4096];
    for (;;) {
        fputs(input.len ? ".. " : ">> ", stdout);
        fflush(stdout);
        if (!fgets(line, sizeof line, stdin)) {
            break;
        }

        size_t n = strlen(line);
        while (n > 0 && (line[n - 1] == '\n' || line[n - 1] == '\r')) {
            line[--n] = '\0';
        }

        if (input.len == 0 && (strcmp(line, "exit") == 0 || strcmp(line, "quit") == 0)) {
            break;
        }

        sb_puts(&input, line);
        sb_putc(&input, '\n');
        if (open_depth(input.data) > 0) {
            continue;
        }

        vm_exec_repl(vm, input.data, env);
        if (vm->esp == 0) {
            vm->envs[vm->esp++] = env; /* restored after an error */
        }

        input.len = 0;
        input.data[0] = '\0';
    }

    sb_free(&input);

    return 0;
}

int main(int argc, char** argv) {
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
#endif
    if (argc >= 2 && (strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0)) {
        usage();
        return 0;
    }

    if (argc >= 2 && (strcmp(argv[1], "--version") == 0 || strcmp(argv[1], "-v") == 0)) {
        printf("pp " PP_VERSION "\n");
        return 0;
    }

    VM* vm = vm_new(argc, argv);
    const int code = argc >= 2 ? vm_run_file(vm, argv[1]) : repl(vm);
    fflush(stdout);

    return code;
}
