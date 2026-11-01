#ifndef PP_VM_H
#define PP_VM_H

#include <setjmp.h>

#include "ast.h"

#define PP_STACK_MAX (1 << 16)
#define PP_ENV_STACK_MAX (1 << 16)
#define PP_FRAMES_MAX 4000
#define PP_MAX_BINDINGS 8

typedef struct {
    const char* name;
    const char* file; /* call site */
    int line;
} Frame;

typedef enum { EX_NORMAL, EX_BREAK, EX_CONTINUE, EX_RETURN } ExecStatus;

/* generic parameter bindings used while matching values against types */
typedef struct Bindings Bindings;
typedef struct {
    ObjString* name;
    Type* type;        /* bound to a type expression (Error<int>) ... */
    ObjEnv* env;       /* ... resolved in this env */
    Bindings* parent;  /* ... with these bindings */
    Value sample;      /* or bound to "the kind of this value" (inferred) */
    bool bound;
} Binding;

struct Bindings {
    Binding items[PP_MAX_BINDINGS];
    int count;
};

typedef ObjModule* (*NativeModuleInit)(VM* vm);
typedef struct {
    const char* name;
    NativeModuleInit init;
} NativeModule;

struct VM {
    Obj* objects;
    size_t num_objects;
    size_t next_gc;
    int gc_disabled;

    Table interned;

    Value* stack;
    int sp;
    ObjEnv** envs;
    int esp;
    Frame* frames;
    int fp;

    ObjEnv* builtins;
    Table modules;
    Table globals; /* name -> ObjOverload of the functions declared `global` */
    Table prec;
    int prec_version;
    Value ret;

    ObjEnum* option_enum;
    ObjEnum* error_enum;

    InlineCache** caches;
    int ncaches;
    int capcaches;

    jmp_buf* err_jmp;
    const char* cur_file;
    int cur_line;

    char** search_paths;
    int nsearch;
    NativeModule* native_modules;
    int nnative_modules;

    int argc;
    char** argv;

    ObjModule* cur_module; /* module whose top level is executing (for export) */

    Type** ctypes; /* concrete types given to lambdas, deduplicated (see types.c) */
    int nctypes;
    int capctypes;

    ObjString* s_and;
    ObjString* s_or;
    ObjString* s_wild;
};

/* interp.c */
VM* vm_new(int argc, char** argv);
int vm_run_file(VM* vm, const char* path);
int vm_exec_repl(VM* vm, const char* src, ObjEnv* env);
PP_NORETURN void rt_error(VM* vm, int line, const char* fmt, ...);
void vm_push(VM* vm, Value v);
Value vm_call(VM* vm, Value callee, int argc, Value* args);
Entry* env_find(ObjEnv* env, ObjString* name, ObjEnv** where);
void env_define(VM* vm, ObjEnv* env, ObjString* name, Value v, uint8_t flags, Type* type);
void env_add_function(VM* vm, ObjEnv* env, ObjString* name, Obj* fn);
ObjEnv* vm_current_env(VM* vm);
void vm_register_ic(VM* vm, InlineCache* ic);
void vm_register_native_module(VM* vm, const char* name, NativeModuleInit init);
Value make_some(VM* vm, Value v);
Value make_none(VM* vm);
Value make_success(VM* vm, Value v);
Value make_fail(VM* vm, Value v);

/* types.c */
int type_match(VM* vm, Type* t, Value v, ObjEnv* env, Bindings* b, bool deep);
bool same_kind(Value a, Value b);
bool resolve_type_value(VM* vm, Type* t, ObjEnv* env, Value* out);
void type_to_string(Type* t, StrBuf* sb);
void sig_to_string(Signature* sig, StrBuf* sb);
Value adopt_callback_types(VM* vm, Type* t, Value v, ObjEnv* env, Bindings* b, ObjString* name);
bool callable_fits(VM* vm, Obj* c, Type** args, int nargs, bool variadic, Type* ret, ObjEnv* fenv, Bindings* fb,
                   ObjString* self, bool impl_mode);

/* gc.c */
void gc_maybe(VM* vm);
void gc_collect(VM* vm);

/* builtins.c / prelude.c */
void builtins_register(VM* vm);
void def_native(VM* vm, ObjEnv* env, const char* name, const char* sig, NativeFn fn);
void def_const(VM* vm, ObjEnv* env, const char* name, Value v);
void def_const_str(VM* vm, ObjEnv* env, const char* name, const char* value);
void module_export_all(ObjModule* m);
void def_submodule(VM* vm, ObjModule* parent, const char* name, NativeModuleInit init);
void mod_io_register(VM* vm);
void mod_os_register(VM* vm);
extern const char* PP_PRELUDE;

#endif
