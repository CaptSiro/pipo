#ifndef PP_CHECK_INT_H
#define PP_CHECK_INT_H

#include <setjmp.h>

#include "check.h"
#include "parser.h"



// The checker's own view of the program (check.c, check_expr.c). Shared by the two files only.

typedef struct CScope CScope;

typedef enum {
    CF_USER,     // a function written in .pp with a body (or declared and defined later)
    CF_NATIVE,   // a C function: a builtin, a native module's, or a `native function` of a library module
    CF_CTOR,     // the constructor of an enum variant: some(T)
    CF_REQ       // a requirement of an interface, called on a value typed with the interface
} CFuncKind;

struct CFunc {
    CFuncKind kind;
    ObjString* name;
    Signature* sig;
    Node* body;          // CF_USER: NULL until defined
    CScope* closure;     // where the signature's types and the body's names resolve
    CModule* module;
    ObjNative* native;   // CF_NATIVE of a builtin or native module
    bool builtin;        // a function of the builtins scope (its static typing may be special, see builtin_result)
    TDecl* en;           // CF_CTOR: the enum; CF_REQ: the interface
    int variant;
    SType* self;         // an interface default copied for a type: the interface name stands for this type
    TDecl* self_iface;
    ObjString** extra_generics; // a default copied for a generic impl: the impl's own parameters
    int nextra;
    ObjString* impl_name;       // CF_NATIVE declared in a .pp module: the name of its C body
    CInstance** insts;
    int ninsts;
    int capinsts;
    bool global;
    bool lambda;
    bool declared_only;  // `function f(int) -> int` without a body (yet)
    int id;
};

struct CFuncSet {
    ObjString* name;
    CFunc** items;
    int count;
    int cap;
};

typedef struct {
    Node* node;
    NodeInfo* info;
} InfoSlot;

struct CInstance {
    CFunc* fn;            // NULL: the top level of a module
    CModule* module;
    SType** params;
    int nparams;
    SType* ret;           // declared, or inferred from the body
    SBindings bindings;   // the generic parameters as this instance binds them
    bool checking;
    bool checked;
    bool infer_ret;       // no declared return type: the body decides
    InfoSlot* slots;      // open addressing, keyed by node
    int nslots;
    int capslots;
    int typed;
    int dyn;
    int id;
    const char* dyn_reason; // the first construct that left something DYN
    int dyn_line;
};

typedef enum {
    SYM_VAR,
    SYM_FUNCS,
    SYM_TYPE,   // tdecl, or `type` a type bound directly (a generic parameter in an instance)
    SYM_MODULE
} SymKind;

typedef struct {
    ObjString* name;
    SymKind kind;
    SType* type;
    bool is_const;
    CFuncSet* funcs;
    TDecl* tdecl;
    CModule* module;
    bool pending;          // set to [] / none() / std:map(): the element types come from the first use
    NodeInfo* init_info;   // ... and are written back to the initialiser's info
} CSym;

struct CScope {
    CScope* parent;
    CSym* syms;
    int count;
    int cap;
    CModule* module;
    CInstance* inst; // the function instance the scope belongs to
};

struct CModule {
    ObjString* name;
    const char* path;      // absolute path of the .pp file, or "native:name"
    CScope* scope;
    ObjString** exp_names; // what importers write
    ObjString** exp_src;   // the symbol in scope
    int nexports;
    int capexports;
    CInstance* top;
    Node* program;         // the parsed file, annotated by the checker (type_of folded)
    bool checked;
    bool native;
    ObjModule* rt;         // a native module's runtime object
};

typedef struct {
    SType* target;         // the implementing type, generics of the impl as ST_PARAM
    ObjString** generics;
    int ngenerics;
    TDecl* iface;
    SType** args;
    int nargs;
} CImpl;

typedef struct {
    SType* target;
    ObjString* name;
    CFunc* fn;
} CImplFn;

typedef struct {
    const char* reason;
    int count;
    const char* file;
    int line;
} DynReason;

typedef struct {
    ObjString* op;
    double prec;
} PrecEntry;

// a type_of(x) call: its static type per instance, folded into a string literal when all agree
typedef struct {
    Node* node;
    CInstance* inst;
    const char* name; // NULL: not known statically
} TypeOfFold;

struct Checker {
    VM* vm;
    jmp_buf jb;
    bool failed;
    CScope* builtins;
    CModule** modules;
    int nmodules;
    int capmodules;
    CModule* main;
    CFuncSet** globals;    // `global function`s by name
    int nglobals;
    int capglobals;
    CImpl* impls;
    int nimpls;
    int capimpls;
    CImplFn* impl_fns;
    int nimpl_fns;
    int capimpl_fns;
    PrecEntry* precs;
    int nprecs;
    int capprecs;
    DynReason* reasons;
    int nreasons;
    int capreasons;
    TDecl* option;
    TDecl* error;
    int typed;
    int dyn;
    int next_id;
    const char* file;      // the file being checked, for messages
    TypeOfFold* folds;
    int nfolds;
    int capfolds;
    CInstance** all_insts;
    int ninsts;
    int capinsts;
    // names compared often
    ObjString* s_and;
    ObjString* s_or;
    ObjString* s_index;
    ObjString* s_index_set;
    ObjString* s_scope;
    ObjString* s_block;
    ObjString* s_callback;
    ObjString* s_unknown;
    ObjString* s_in;
};

// the state of checking one body
typedef struct Ctx {
    Checker* c;
    CScope* scope;
    CInstance* inst;
    CModule* module;
    SType* ret_want;      // the declared return type, NULL while inferring
    SType* ret_got;       // inferring: the join of the returned types
    bool returned;        // inferring: a return was seen
    int loops;
    bool in_value;        // an if / match used as a value: no break / continue / return
    bool in_scope_value;  // ... or the block of a scope call used as a value
    SType* scope_value;   // in a scope call's block: the type of `scope`
    bool in_scope_fn;     // directly in a scope function's body: defer is allowed
} Ctx;

// check.c
PP_NORETURN void cerr(Checker* c, int line, const char* fmt, ...);
SType* dyn_type(Ctx* x, Node* n, const char* reason);
CScope* scope_new(CScope* parent, CModule* m, CInstance* inst);
CSym* scope_lookup(CScope* s, ObjString* name);
CSym* scope_lookup_here(CScope* s, ObjString* name);
CSym* scope_define(Checker* c, CScope* s, ObjString* name, SymKind kind, int line);
void scope_add_func(Checker* c, CScope* s, ObjString* name, CFunc* f);
CFuncSet* global_funcs(Checker* c, ObjString* name);
CFunc* func_new(Checker* c, CFuncKind kind, ObjString* name, Signature* sig, CScope* closure);
SType* resolve_type(Ctx* x, Type* t, CScope* s, SBindings* gen);
SType* subst(SType* t, SBindings* b);
SType* st_record_fill(Checker* c, SType* t);
SType* enum_type(Checker* c, TDecl* en, SType** args, int nargs);
SType* option_of(Checker* c, SType* t);
SType* variant_payload(Checker* c, SType* en, int variant, int k);
CModule* load_module(Ctx* x, Node* imp, int line);
bool cmodule_export(Checker* c, CModule* m, ObjString* name, CSym* out);
double prec_of(Checker* c, ObjString* op);
void set_prec(Checker* c, ObjString* op, double p);
NodeInfo* info_put(CInstance* inst, Node* n);
CInstance* cinstance_new(Checker* c, CFunc* fn, CModule* m);
void count_type(Ctx* x, SType* t);
SType* named_record(Checker* c, TDecl* d);
void add_fold(Checker* c, Node* n, CInstance* inst, const char* name);
void checker_hooks(Checker* c);
const char* module_lib_path(CModule* m);
CInstance* checker_declared_instance(Checker* c, CFunc* f, SType** sargs, int sargc, SType*** params_out, int* nparams_out); // "std", "os/fs": the name a library module's C half is registered under, or NULL
void apply_folds(Checker* c);

// check_expr.c
SType* check_expr(Ctx* x, Node* n, SType* expect);
void check_stmt(Ctx* x, Node* n);
SType* check_block(Ctx* x, Node* block, bool new_scope, SType* expect);
void check_body(Checker* c, CInstance* inst);
CInstance* instantiate(Ctx* x, CFunc* f, SType** args, int argc, SBindings* b, int line);
SType* func_value_type(Ctx* x, CFunc* f);
void declare_function(Ctx* x, Node* n);
bool st_implements(Checker* c, TDecl* iface, SType** args, int nargs, SType* got);



#endif // PP_CHECK_INT_H
