#ifndef PP_CHECK_H
#define PP_CHECK_H

#include "vm.h"



// ---------------------------------------------------------------- static types

// The static type of an expression, as the checker (check.c) sees it. Types are
// allocated in the checker's arena and never freed one by one.
typedef enum {
    ST_DYN,     // not modelled by the checker: no errors are reported about it, backends refuse it
    ST_NEVER,   // the expression does not finish (panic, a call that never returns); fits everywhere
    ST_UNKNOWN, // `unknown` (`any`): any value, boxed with its runtime type, like a void* that knows its type
    ST_VOID,
    ST_BOOL,
    ST_INT,
    ST_FLOAT,
    ST_STRING,
    ST_RANGE,
    ST_ARRAY,   // elem
    ST_TUPLE,   // args: the element types
    ST_RECORD,  // fields; decl != NULL: nominal (declared with `type X = { ... }`)
    ST_OBJECT,  // `object`: a record whose fields are not known statically
    ST_ENUM,    // decl, args
    ST_NATIVE,  // decl (Map, Set, types:Int8Array, fs:File, ...), args
    ST_IFACE,   // decl, args: any value whose type implements the interface
    ST_FUNC,    // args: parameter types, elem: return type (NULL = not known), variadic
    ST_FUNCTION,// `function`: any callable whose signature is not known statically
    ST_UNION,   // args: the members, flattened, at least two
    ST_PARAM,   // a generic parameter that is not bound yet (name)
    ST_OVERLOAD,// a function name used as a value; funcs
    ST_MODULE,  // module
    ST_TYPE,    // a type used as a value (an enum for Color:RED, int as a converter); decl / elem
    ST_BLOCK    // Block<T> / Callback<T> of a scope; elem: T, variadic: true for Callback
} STKind;

typedef struct SType SType;
typedef struct TDecl TDecl;
typedef struct CFuncSet CFuncSet;
typedef struct CModule CModule;

struct SType {
    STKind kind;
    SType* elem;
    SType** args;
    int nargs;
    bool variadic;
    ObjString** fnames; // ST_RECORD
    SType** ftypes;
    int nfields;
    TDecl* decl;        // ST_RECORD (nominal), ST_ENUM, ST_NATIVE, ST_IFACE, ST_TYPE
    ObjString* name;    // ST_PARAM
    CFuncSet* funcs;    // ST_OVERLOAD
    CModule* module;    // ST_MODULE
    ObjString* alias;   // the name of the `type X = ...` it came from, for messages
    int arity;          // ST_FUNCTION standing for a lambda not typed yet: its parameter count + 1, 0 = any
};

typedef enum {
    TD_ALIAS,  // type X = ... (a record when def is TY_RECORD)
    TD_ENUM,
    TD_NATIVE, // native type Map<K, V>
    TD_IFACE
} TDeclKind;

typedef struct {
    ObjString* name;
    bool is_ctor;
    Type** payload;
    int npayload;
} CVariant;

typedef struct CScope CScope;

struct TDecl {
    TDeclKind kind;
    ObjString* name;
    ObjString** generics;
    Type** defaults;
    int ngenerics;
    Type* def;           // TD_ALIAS
    CScope* scope;       // where def / payloads resolve
    CModule* module;
    CVariant* variants;  // TD_ENUM
    int nvariants;
    Type* value_type;    // TD_ENUM: enum string X { ... }
    NativeTypeKind which;// TD_NATIVE
    Signature** reqs;    // TD_IFACE
    Node** bodies;       // TD_IFACE: default implementation per requirement, NULL = none
    int nreqs;
    Type** parents;
    int nparents;
    SType* record;       // TD_ALIAS of a record, without type arguments: resolved lazily
    Node* node;          // TD_ENUM: its declaration (the variants' values, for compiling targets)
    int id;
};

SType* st_new(STKind kind);
SType* st_prim(STKind kind);           // shared instance of DYN, NEVER, UNKNOWN, VOID, BOOL, INT, FLOAT, STRING, RANGE, OBJECT, FUNCTION
SType* st_array(SType* elem);
SType* st_union(SType* a, SType* b);   // flattened, deduplicated; a single member is returned as is
bool st_equal(const SType* a, const SType* b);
bool st_is_dyn(const SType* t);        // DYN anywhere inside
bool st_is_numeric(const SType* t);    // int, float, or a union of both
bool st_has_param(const SType* t);     // an unbound generic parameter anywhere inside
void st_print(const SType* t, StrBuf* sb);
const char* st_name(const SType* t);   // printed into a buffer that lives until the next call

// how well a value of type `got` fits where `want` is expected: 0 = not at all.
// The scores follow the runtime's (types.c), so the checker picks the overload the interpreter would.
typedef struct {
    ObjString* names[PP_MAX_BINDINGS];
    SType* types[PP_MAX_BINDINGS];
    int count;
} SBindings;

int st_fit(SType* want, SType* got, SBindings* b);



// ---------------------------------------------------------------- checker

typedef struct Checker Checker;

// What the checker learnt about one node of one function instance.
typedef struct CInstance CInstance;
typedef struct CFunc CFunc;

// one call the checker resolved: an operator of a chain, a call, an index
typedef struct {
    SType* type;       // the result
    CInstance* target; // the user function instance called ...
    CFunc* func;       // ... or the native / constructor / requirement called
    int op;            // a built-in operation the backends emit directly (CheckOp), 0 = none
    bool dispatch;     // other candidates could fit the runtime values: needs dynamic dispatch
    CFunc** cands;     // dispatch: every candidate that fits the static types, the most specific first
    int ncands;
} CCall;

// N_CHAIN: the operator tree as the checker built it with the precedences in force
typedef struct {
    int nops;
    int root;
    int* left;  // like Node.as.chain: < 0 is operand -x - 1
    int* right;
    CCall* calls;
} ChainInfo;

typedef struct {
    Node* node;
    SType* type;       // the value's static type
    CCall call;        // N_CALL / N_METHOD / N_INDEX / S_ASSIGN of an index / N_SCOPECALL;
                       // S_FUNCDECL: call.func is the function it declares
    CCall call2;       // S_ASSIGN `c[k] op= v`: the []= that stores the result
    ChainInfo* chain;  // N_CHAIN
    void* backend;     // free for a backend's own use
} NodeInfo;

// built-in operations on primitive types
typedef enum {
    COP_NONE,
    COP_ADD_I, COP_SUB_I, COP_MUL_I, COP_DIV_I, COP_MOD_I, COP_NEG_I,
    COP_ADD_F, COP_SUB_F, COP_MUL_F, COP_DIV_F, COP_MOD_F, COP_NEG_F, COP_POW_F,
    COP_EQ_I, COP_NE_I, COP_LT_I, COP_GT_I, COP_LE_I, COP_GE_I,
    COP_EQ_F, COP_NE_F, COP_LT_F, COP_GT_F, COP_LE_F, COP_GE_F,
    COP_EQ_B, COP_NE_B, COP_NOT,
    COP_CONCAT, COP_EQ_S, COP_NE_S, COP_LT_S, COP_GT_S, COP_LE_S, COP_GE_S,
    COP_LEN_S, COP_LEN_A,
    COP_RANGE, COP_RANGE_TO, COP_RANGE_INCL, COP_RANGE_TO_INCL,
    COP_POW_I,
    COP_COUNT
} CheckOp;

Checker* checker_new(VM* vm);

// Checks a program and every module it imports. Prints errors as `file:line: error: message`
// and returns false when there were any.
bool check_program(Checker* c, Node* program, const char* file);

// One line per kind of construct the checker could not type (DYN), with counts.
void checker_report(Checker* c, FILE* out);

// number of expressions typed / left DYN in the checked program
int checker_typed_count(Checker* c);
int checker_dyn_count(Checker* c);

// the instance that holds the top level code of the checked program
CInstance* checker_main(Checker* c);
Node* checker_module_ast(Checker* c, const char* abs_path); // the checked tree of an imported file, or NULL
NodeInfo* instance_info(CInstance* inst, Node* n);



#endif // PP_CHECK_H
