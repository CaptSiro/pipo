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
    const char* file; // call site
    int line;
} Frame;

// EX_SUSPEND: a generator's body paused at an `emit` (interp.c, generators only)
typedef enum { EX_NORMAL, EX_BREAK, EX_CONTINUE, EX_RETURN, EX_SUSPEND } ExecStatus;

// A function whose body calls lift() installs one of these: lift() on none() / fail(..)
// longjmps here and the function returns that value. Only functions the parser marked
// (Signature.uses_lift) pay for the setjmp.
typedef struct LiftTarget {
    jmp_buf jb;
    struct LiftTarget* prev;
    jmp_buf* err_jmp;
    int fp; // vm->fp of the function's own frame: lift's native frame sits right above it
    int sp;
    int esp;
    int gc_disabled;
    struct ScopeCall* scope_top;
    Value value;
} LiftTarget;

// A running scope call `_name args { body }`. When the body leaves early (return, break,
// continue, a failing lift), the block longjmps here: the scope function is unwound, its
// current `defer` block runs, and the exit continues from the call site.
typedef struct ScopeCall {
    jmp_buf jb;
    struct ScopeCall* prev;
    jmp_buf* err_jmp;
    LiftTarget* lift_top;
    int fp;
    int sp;
    int esp;
    int gc_disabled;
    const char* file;
    int line;
    ObjString* name; // the scope's name, for messages
    int slot;        // vm->stack[slot]: the value of an early return, [slot + 1]: the current defer block
    ExecStatus exit; // EX_NORMAL until the body leaves early
} ScopeCall;

// generic parameter bindings used while matching values against types
typedef struct Bindings Bindings;
typedef struct {
    ObjString* name;
    Type* type;        // bound to a type expression (Error<int>) ...
    ObjEnv* env;       // ... resolved in this env
    Bindings* parent;  // ... with these bindings
    Value sample;      // or bound to "the kind of this value" (inferred)
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

// the C half of a mixed library module (NativeImpl is in value.h)
typedef struct {
    const char* module; // library module path: "geometry", "os:fs"
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
    Table globals; // name -> ObjOverload of the functions declared `global`
    Table opeq_names; // every function name ending in '=' ever declared (+=, <=, []=): `x op= v` skips the in-place lookup otherwise
    Table prec;
    int prec_version;
    Value ret;

    ObjEnum* option_enum;
    ObjEnum* error_enum;

    ObjNativeType* t_map; // Map, Set: bound in lib/std.pp by `native type`
    ObjNativeType* t_set;
    ObjNativeType* t_generator; // Generator<T>: bound in the prelude by `native type`
    Table range_impl_fns;       // what `impl ... for Range<T>` recorded (iter for Range<Day>, ...)
    ObjNativeType** native_types; // NativeClass types registered by modules (vm_register_native_types), permanent
    int nnative_types;
    Obj* tuple_ctor;      // the built-in tuple(...) function, recognised in patterns
    ObjString* s_index;   // "[]" and "[]=": x[i] and x[i] = v call these
    ObjString* s_index_set;
    Obj* index_builtin;   // the builtins' overload sets of [] and []=, for the fast path
    Obj* index_set_builtin;
    int index_overloads;  // [] / []= overload sets created outside the builtins; 0 = x[i] can skip the lookup

    InlineCache** caches;
    int ncaches;
    int capcaches;

    jmp_buf* err_jmp;
    LiftTarget* lift_top;
    ScopeCall* scope_top; // innermost running scope call: `defer` replaces its cleanup
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

    Node* (*preparsed)(VM* vm, const char* abs_path); // the checked tree of a module file, or NULL
    Value (*closure_call)(VM* vm, Obj* closure, int argc, Value* args, int line); // runs an OBJ_CLOSURE
    Value (*gen_resume)(VM* vm, ObjGenerator* g, bool* done); // runs a GEN_BYTECODE generator to its next value
    Value bc_roots; // the bytecode target's constants and the objects its natives come from
    void* checker;

    ObjModule* cur_module; // module whose top level is executing (for export)

    Type** ctypes; // concrete types given to lambdas, deduplicated (see types.c)
    int nctypes;
    int capctypes;

    ObjString* s_and;
    ObjString* s_or;
    ObjString* s_wild;
    ObjString* s_block;    // "Block", "Callback": the built-in types of a scope's block parameter
    ObjString* s_callback;
    ObjString* s_scope;    // "scope": the variable holding the value a block was called with
};

// interp.c
VM* vm_new(int argc, char** argv);
int vm_run_file(VM* vm, const char* path);
Node* vm_parse_file(VM* vm, const char* path, const char** abs_out, int* code); // NULL: *code is the exit code
int vm_run_program(VM* vm, Node* program, const char* abs);
int vm_exec_repl(VM* vm, const char* src, ObjEnv* env);
PP_NORETURN void rt_error(VM* vm, int line, const char* fmt, ...);
Value vm_get_field(VM* vm, Value obj, ObjString* name, int line);   // obj:name
void vm_set_field(VM* vm, Value obj, ObjString* name, Value v, int line); // obj:name = v
Value vm_index_get(VM* vm, Value obj, Value idx, int line);         // the built-in obj[idx]
void vm_index_set(VM* vm, Value obj, Value idx, Value v, int line); // the built-in obj[idx] = v
ObjEnum* vm_enum_new(VM* vm, ObjString* name, int nvariants);       // permanent; fill variants with vm_enum_variant
void vm_enum_variant(VM* vm, ObjEnum* en, int i, ObjString* name, bool is_ctor, int npayload, Value value);
ObjTypeDef* vm_typedef_new(VM* vm, ObjString* name);                // permanent; a record type for tagging objects
char* vm_find_module_file(VM* vm, const char* path, bool is_file, const char* from_file); // absolute path or NULL; caller frees
NativeModuleInit vm_native_module(VM* vm, const char* name); // a registered native module's init, or NULL
const NativeImpl* vm_native_impls(VM* vm, const char* path); // the C bodies of library module `path`, or NULL
void vm_push(VM* vm, Value v);
Value vm_call(VM* vm, Value callee, int argc, Value* args);
Entry* env_find(ObjEnv* env, ObjString* name, ObjEnv** where);
void env_define(VM* vm, ObjEnv* env, ObjString* name, Value v, uint8_t flags, Type* type);
void env_add_function(VM* vm, ObjEnv* env, ObjString* name, Obj* fn);
ObjEnv* vm_current_env(VM* vm);
void vm_register_ic(VM* vm, InlineCache* ic);
void vm_register_native_module(VM* vm, const char* name, NativeModuleInit init);
void vm_register_native_impls(VM* vm, const char* module, const NativeImpl* impls);
// the name the prelude's `native function`s (Map, Set) are registered under; no library file can have it
#define PP_PRELUDE_NATIVES "<prelude>"
void vm_register_native_types(VM* vm, const char* module, const NativeClass* classes); // terminated by {NULL}
ObjNativeType* vm_native_type(VM* vm, const char* module, const char* name); // a registered class's type, or NULL
Value make_some(VM* vm, Value v);
Value make_none(VM* vm);
// Record conversion: a record literal may leave out its T? fields (none()), write a field a_b as a-b
// (tag attributes), and, for a record type with a rest field, have fields the type does not name
// (they move into the rest map). record_convert does that to an object taking a record type.
bool type_is_option(const Type* t);
bool record_key_names(const ObjString* key, const ObjString* field); // key is field, or field with '_' written '-'
void record_convert(VM* vm, ObjObject* o, const Type* record);
// a record (an untyped object) where a Map<string, V> is expected: a new map of its fields; other values as they are
Value record_as_map(VM* vm, Type* t, Value v, ObjEnv* env);
// decode(m, T): a Map<string, unknown> checked and converted into the record type T, as an Error<T>
Value record_decode(VM* vm, Value map, Value type, int line);
Value make_success(VM* vm, Value v);
Value make_fail(VM* vm, Value v);
Value vm_lift(VM* vm, Value v);

// types.c
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

// collections.c
uint32_t hash_key(VM* vm, Value key, int line); // runtime error for keys that cannot be hashed
Value native_type_construct(VM* vm, ObjNativeType* t, int argc, Value* args, int line);

// gc.c
void gc_maybe(VM* vm);
void gc_collect(VM* vm);

// builtins.c / prelude.c / src/modules/
void builtins_register(VM* vm);
void def_native(VM* vm, ObjEnv* env, const char* name, const char* sig, NativeFn fn);

// generators (interp.c): a new one of the given kind, and its next value (false: it is done)
ObjGenerator* gen_new(VM* vm, GenKind kind, ObjString* name);
bool gen_next(VM* vm, ObjGenerator* g, Value* out, int line);
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

// modules/types.c: the typed arrays of lib/types.pp (Int8Array ... Float64Array).
// Fixed length, elements stored as raw C numbers, zero-initialised.
typedef enum { TA_I8, TA_I16, TA_I32, TA_I64, TA_F32, TA_F64 } TypedArrayKind;
typedef struct {
    ObjInstance base;
    TypedArrayKind kind;
    int64_t length;
    void* data; // length elements of the kind's C type (int8_t, ..., double)
} ObjTypedArray;
bool is_typed_array(Value v);
ObjTypedArray* typed_array_new(VM* vm, TypedArrayKind kind, int64_t length);

// extern.c: `extern "lib" function f(...) -> T`, a C function of a DLL / shared library (md/EXTERN.md).
// extern_new reads the C types from the signature (NULL and a message in err when C has none);
// the library is loaded on the first extern_call.
Extern* extern_new(Signature* sig, const char* lib, const char* symbol, char* err, size_t errn);
Value extern_call(VM* vm, Extern* e, int argc, Value* args);
Value extern_native(VM* vm, int argc, Value* args); // the NativeFn of extern ObjNatives; never called

// modules/pp.c: directory of the file the running code is written in (caller frees)
char* vm_script_dir(VM* vm);
extern const char* PP_PRELUDE;



#endif // PP_VM_H
