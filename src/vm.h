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

/* the C half of a mixed library module (NativeImpl is in value.h) */
typedef struct {
    const char* module; /* library module path: "geometry", "os:fs" */
    const NativeImpl* impls;
} NativePart;

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
    Table opeq_names; /* every function name ending in '=' ever declared (+=, <=, []=): `x op= v` skips the in-place lookup otherwise */
    Table prec;
    int prec_version;
    Value ret;

    ObjEnum* option_enum;
    ObjEnum* error_enum;

    ObjNativeType* t_map; /* std:Map, std:Set: bound in lib/std.pp by `native type` */
    ObjNativeType* t_set;
    ObjNativeType** native_types; /* NativeClass types registered by modules (vm_register_native_types), permanent */
    int nnative_types;
    Obj* tuple_ctor;      /* the built-in tuple(...) function, recognised in patterns */
    ObjString* s_index;   /* "[]" and "[]=": x[i] and x[i] = v call these */
    ObjString* s_index_set;
    Obj* index_builtin;   /* the builtins' overload sets of [] and []=, for the fast path */
    Obj* index_set_builtin;
    int index_overloads;  /* [] / []= overload sets created outside the builtins; 0 = x[i] can skip the lookup */

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
    NativePart* native_parts;
    int nnative_parts;

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
void vm_register_native_impls(VM* vm, const char* module, const NativeImpl* impls);
void vm_register_native_types(VM* vm, const char* module, const NativeClass* classes); /* terminated by {NULL} */
ObjNativeType* vm_native_type(VM* vm, const char* module, const char* name); /* a registered class's type, or NULL */
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
Type* concrete_type(VM* vm, Type* t, ObjEnv* env, Bindings* b);
bool interface_extends(ObjInterface* child, ObjInterface* parent);

/* collections.c */
uint32_t hash_key(VM* vm, Value key, int line); /* runtime error for keys that cannot be hashed */
Value native_type_construct(VM* vm, ObjNativeType* t, int argc, Value* args, int line);

/* gc.c */
void gc_maybe(VM* vm);
void gc_collect(VM* vm);

/* builtins.c / prelude.c / src/modules/ */
void builtins_register(VM* vm);
void def_native(VM* vm, ObjEnv* env, const char* name, const char* sig, NativeFn fn);
void def_const(VM* vm, ObjEnv* env, const char* name, Value v);
void def_const_str(VM* vm, ObjEnv* env, const char* name, const char* value);
void module_export_all(ObjModule* m);
void def_submodule(VM* vm, ObjModule* parent, const char* name, NativeModuleInit init);
void mod_io_register(VM* vm);
void mod_os_register(VM* vm);
void mod_pp_register(VM* vm);
void mod_std_register(VM* vm);
void mod_fs_register(VM* vm);
void mod_types_register(VM* vm);
void mod_math_register(VM* vm);

/*
 * modules/types.c: the typed arrays of lib/types.pp (Int8Array ... Float64Array).
 * Fixed length, elements stored as raw C numbers, zero-initialised.
 */
typedef enum { TA_I8, TA_I16, TA_I32, TA_I64, TA_F32, TA_F64 } TypedArrayKind;
typedef struct {
    ObjInstance base;
    TypedArrayKind kind;
    int64_t length;
    void* data; /* length elements of the kind's C type (int8_t, ..., double) */
} ObjTypedArray;
bool is_typed_array(Value v);
ObjTypedArray* typed_array_new(VM* vm, TypedArrayKind kind, int64_t length);

/* modules/pp.c: directory of the file the running code is written in (caller frees) */
char* vm_script_dir(VM* vm);
extern const char* PP_PRELUDE;

#endif
