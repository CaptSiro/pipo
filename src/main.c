#include "backend.h"
#include "check.h"
#include "vm.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif



typedef enum {
    TARGET_INTERP,   // check, then run with the tree-walking interpreter
    TARGET_CHECK,    // only check
    TARGET_BYTECODE, // check, compile to bytecode and run it in the bytecode VM (the PC / x86 target)
    TARGET_WASM      // check and compile to a WebAssembly module (.wasm + .mjs loader + .html page)
} Target;

typedef struct {
    const char* name;
    Target target;
    const char* about;
} TargetInfo;

static const TargetInfo TARGETS[] = {
    {"interp", TARGET_INTERP, "check, then run in the tree-walking interpreter (default)"},
    {"check", TARGET_CHECK, "only check the program's static types"},
    {"bytecode", TARGET_BYTECODE, "check, compile to bytecode and run it in the bytecode VM (PC, x86 / x64)"},
    {"x86", TARGET_BYTECODE, "the same as bytecode"},
    {"wasm", TARGET_WASM, "check and compile to WebAssembly: <out>.wasm, <out>.mjs and <out>.html"},
    {NULL, TARGET_INTERP, NULL},
};



static void usage(void) {
    printf("pp " PP_VERSION "\n"
           "usage:\n"
           "  pp [options] <file.pp> [args...]   run a program\n"
           "  pp                                 start the interactive REPL\n"
           "  pp --version                       print the version\n"
           "  pp --help                          show this message\n"
           "\n"
           "options:\n"
           "  --target=<target>   what to do with the program (--target=list shows them)\n"
           "  -o <path>           output path for compiling targets (wasm: without extension)\n"
           "  --report            print how much of the program the checker typed statically\n"
           "  --no-check          skip the static checker (interp target only)\n"
           "  --dump              print the compiled bytecode (bytecode target)\n"
           "\n"
           "modules are searched next to the importing file, in <pp dir>/lib\n"
           "and in the directories listed in the PP_PATH environment variable\n");
}

static void list_targets(void) {
    for (int i = 0; TARGETS[i].name; ++i) {
        printf("  %-10s %s\n", TARGETS[i].name, TARGETS[i].about);
    }
}

// counts unclosed (, [ and { so the REPL knows when to ask for more input
static int open_depth(const char* s) {
    int depth = 0;
    char quote = 0;
    for (const char* p = s; *p; ++p) {
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
    vm->envs[vm->esp++] = env; // keeps the REPL scope alive across collections
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
            vm->envs[vm->esp++] = env; // restored after an error
        }

        input.len = 0;
        input.data[0] = '\0';
    }

    sb_free(&input);

    return 0;
}

static Node* preparsed(VM* vm, const char* abs_path) {
    return vm->checker ? checker_module_ast((Checker*)vm->checker, abs_path) : NULL;
}

// the output path without extension: -o, else the script's path without .pp
static char* output_base(const char* out, const char* script) {
    if (out) {
        return pp_strdup(out);
    }

    const size_t n = strlen(script);
    if (n > 3 && strcmp(script + n - 3, ".pp") == 0) {
        return pp_strndup(script, n - 3);
    }

    return pp_strdup(script);
}

int main(int argc, char** argv) {
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
#endif
    Target target = TARGET_INTERP;
    bool report = false;
    bool no_check = false;
    bool dump = false;
    const char* out = NULL;
    int first = 1;
    for (; first < argc && argv[first][0] == '-'; ++first) {
        const char* a = argv[first];
        if (strcmp(a, "--help") == 0 || strcmp(a, "-h") == 0) {
            usage();

            return 0;
        }

        if (strcmp(a, "--version") == 0 || strcmp(a, "-v") == 0) {
            printf("pp " PP_VERSION "\n");

            return 0;
        }

        if (strncmp(a, "--target=", 9) == 0) {
            const char* name = a + 9;
            if (strcmp(name, "list") == 0) {
                list_targets();

                return 0;
            }

            bool found = false;
            for (int i = 0; TARGETS[i].name; ++i) {
                if (strcmp(TARGETS[i].name, name) == 0) {
                    target = TARGETS[i].target;
                    found = true;
                }
            }

            if (!found) {
                fprintf(stderr, "error: unknown target '%s'; the targets are:\n", name);
                list_targets();

                return 64;
            }

            continue;
        }

        if (strcmp(a, "-o") == 0 && first + 1 < argc) {
            out = argv[++first];
            continue;
        }

        if (strcmp(a, "--report") == 0) {
            report = true;
            continue;
        }

        if (strcmp(a, "--no-check") == 0) {
            no_check = true;
            continue;
        }

        if (strcmp(a, "--dump") == 0) {
            dump = true;
            continue;
        }

        fprintf(stderr, "error: unknown option '%s' (pp --help lists them)\n", a);

        return 64;
    }

    // the program sees its own path and arguments only: argv[0], script, args...
    char** pargv = pp_malloc(sizeof(char*) * (size_t)(argc + 1));
    int pargc = 0;
    pargv[pargc++] = argv[0];
    for (int i = first; i < argc; ++i) {
        pargv[pargc++] = argv[i];
    }

    pargv[pargc] = NULL;
    VM* vm = vm_new(pargc, pargv);
    if (first >= argc) {
        return repl(vm);
    }

    const char* script = argv[first];
    const char* abs = NULL;
    int code = 0;
    Node* program = vm_parse_file(vm, script, &abs, &code);
    if (!program) {
        return code;
    }

    Checker* checker = NULL;
    if (!no_check || target != TARGET_INTERP) {
        checker = checker_new(vm);
        vm->checker = checker;
        vm->preparsed = preparsed;
        if (!check_program(checker, program, abs)) {
            return 65;
        }

        if (report) {
            checker_report(checker, stderr);
        }
    }

    switch (target) {
        case TARGET_CHECK: code = 0; break;
        case TARGET_INTERP: code = vm_run_program(vm, program, abs); break;
        case TARGET_BYTECODE: code = bytecode_run(vm, checker, dump); break;
        case TARGET_WASM: {
            char* base = output_base(out, script);
            code = wasm_compile(vm, checker, base);
            free(base);
            break;
        }
    }

    fflush(stdout);

    return code;
}
