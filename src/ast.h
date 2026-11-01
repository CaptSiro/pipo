#ifndef PP_AST_H
#define PP_AST_H

#include "value.h"

/* ------------------------------------------------------------------- types */

typedef enum {
    TY_PRIM,
    TY_NAME,   /* user / generic / enum type, optionally with type arguments */
    TY_ARRAY,
    TY_UNION,
    TY_RECORD
} TypeKind;

typedef enum {
    PRIM_INT,
    PRIM_FLOAT,
    PRIM_STRING,
    PRIM_BOOL,
    PRIM_VOID,
    PRIM_RANGE,
    PRIM_FUNCTION,
    PRIM_OBJECT,
    PRIM_ANY,
    PRIM_TYPE
} Prim;

struct Type {
    TypeKind kind;
    int line;
    Prim prim;           /* TY_PRIM */
    ObjString* name;     /* TY_NAME */
    ObjString* qual;     /* TY_NAME qualified by a module: qual:name */
    Type** args;
    int nargs;
    Type* inner;         /* TY_ARRAY */
    Type* a;             /* TY_UNION */
    Type* b;
    ObjString** fnames;  /* TY_RECORD */
    Type** ftypes;
    int nfields;
};

/* monomorphic inline cache for overload resolution at a call site */
#define IC_SETS 3
typedef struct InlineCache {
    Obj* sets[IC_SETS];        /* merged candidate sets: local, receiver's module, global; sets[0] NULL = empty */
    int counts[IC_SETS];
    int argc;
    uint8_t kinds[4];
    Obj* target;
} InlineCache;

/* -------------------------------------------------------------------- nodes */

typedef enum {
    /* expressions */
    N_INT,
    N_FLOAT,
    N_STRING,
    N_BOOL,
    N_IDENT,
    N_ARRAY,
    N_OBJECT,
    N_CALL,
    N_METHOD,
    N_FIELD,
    N_INDEX,
    N_CHAIN,
    N_FUNC,
    N_IF,
    N_MATCH,
    N_BLOCK,
    /* statements */
    S_EXPR,
    S_LET,
    S_ASSIGN,
    S_FUNCDECL,
    S_TYPE,
    S_ENUM,
    S_IMPORT,
    S_EXPORT,
    S_PRECEDENCE,
    S_FOR,
    S_WHILE,
    S_BREAK,
    S_CONTINUE,
    S_RETURN
} NodeKind;

typedef struct {
    Node** items;
    int count;
    int cap;
} NodeList;

typedef enum { P_WILD, P_LITERAL, P_BIND, P_VARIANT } PatKind;

typedef struct Pattern {
    PatKind kind;
    int line;
    Node* lit;        /* P_LITERAL */
    ObjString* name;  /* P_BIND */
    Node* ctor;       /* P_VARIANT: expression naming the variant */
    struct Pattern** subs;
    int nsubs;
    bool has_parens;
} Pattern;

typedef struct {
    Pattern* pat;
    Node* guard;
    Node* body;
    bool body_is_block;
} MatchArm;

typedef struct {
    ObjString* name;
    bool is_ctor;
    Type** payload;
    int npayload;
    Node* value;
} VariantDecl;

struct Node {
    NodeKind kind;
    int line;
    union {
        int64_t ival;
        double fval;
        bool bval;
        ObjString* str; /* N_STRING value, N_IDENT name */
        struct { NodeList items; } array;
        struct { ObjString** keys; NodeList values; } object;
        struct { Node* callee; NodeList args; InlineCache* ic; } call;
        /* recv.callee(args) == callee(recv, args); callee is a name or a:b:c path */
        struct { Node* recv; Node* callee; NodeList args; InlineCache* ic; } method;
        struct { Node* obj; ObjString* name; } field;
        struct { Node* obj; Node* index; } index;
        struct {
            NodeList operands;
            ObjString** ops;
            int nops;
            InlineCache* ics;
            /* operator tree, rebuilt whenever precedences change */
            double* precs;
            int* left;
            int* right;
            int root;
            int version;
        } chain;
        struct { Signature* sig; Node* body; bool global; } func;
        struct { Node* cond; Pattern* pat; bool pat_mut; Node* then_b; Node* else_b; } if_;
        struct { Node* subject; MatchArm* arms; int narms; } match;
        struct { NodeList stmts; bool scoped; } block;
        struct { Node* expr; } expr;
        struct { bool is_const; Type* type; ObjString* name; Node* init; } let;
        struct { Node* target; ObjString* op; Node* value; InlineCache* ic; } assign;
        struct { ObjString* name; ObjString** generics; int ngenerics; Type* def; } type_decl;
        struct {
            ObjString* name;
            ObjString** generics;
            Type** defaults;
            int ngenerics;
            Type* value_type;
            VariantDecl* variants;
            int nvariants;
        } enum_decl;
        struct { ObjString* path; ObjString* alias; ObjString** names; int nnames; bool destructure; bool is_file; } import;
        struct { ObjString** names; ObjString** aliases; int count; } export_;
        struct { ObjString* op; int rel; ObjString* other; } prec;
        struct { bool mut; ObjString* var1; ObjString* var2; Node* iter; Node* body; } for_;
        struct { Node* cond; Node* body; } while_;
        struct { Node* value; } ret;
    } as;
};

#endif
