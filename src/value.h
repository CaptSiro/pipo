#ifndef PP_VALUE_H
#define PP_VALUE_H

#include "common.h"

typedef struct VM VM;
typedef struct Obj Obj;
typedef struct ObjString ObjString;
typedef struct ObjEnv ObjEnv;
typedef struct ObjTypeDef ObjTypeDef;
typedef struct ObjEnum ObjEnum;
typedef struct ObjModule ObjModule;
typedef struct ObjEnumValue ObjEnumValue;
typedef struct ObjVariantCtor ObjVariantCtor;
typedef struct ObjInterface ObjInterface;
typedef struct Type Type;
typedef struct Node Node;

/* ------------------------------------------------------------------ values */

typedef enum {
    VAL_VOID,  /* "no value" - result of statements / void functions */
    VAL_BOOL,
    VAL_INT,
    VAL_FLOAT,
    VAL_OBJ
} ValueKind;

typedef struct Value {
    ValueKind kind;
    union {
        bool b;
        int64_t i;
        double f;
        Obj* o;
    } as;
} Value;

static inline Value void_val(void) {
    Value v;
    v.kind = VAL_VOID;
    v.as.i = 0;
    return v;
}

static inline Value bool_val(bool b) {
    Value v;
    v.kind = VAL_BOOL;
    v.as.i = 0;
    v.as.b = b;
    return v;
}

static inline Value int_val(int64_t i) {
    Value v;
    v.kind = VAL_INT;
    v.as.i = i;
    return v;
}

static inline Value float_val(double f) {
    Value v;
    v.kind = VAL_FLOAT;
    v.as.f = f;
    return v;
}

static inline Value obj_val(void* o) {
    Value v;
    v.kind = VAL_OBJ;
    v.as.o = (Obj*)o;
    return v;
}

/* ----------------------------------------------------------------- objects */

typedef enum {
    OBJ_STRING,
    OBJ_ARRAY,
    OBJ_OBJECT,       /* JSON-like record */
    OBJ_RANGE,
    OBJ_FUNCTION,     /* user function */
    OBJ_NATIVE,       /* internal (C) function */
    OBJ_OVERLOAD,     /* set of functions sharing one name */
    OBJ_ENUM,         /* enum definition (also a type) */
    OBJ_VARIANT_CTOR, /* constructor of an enum variant, e.g. some(...) */
    OBJ_ENUM_VALUE,   /* instance of an enum variant */
    OBJ_TYPEDEF,      /* `type X = ...` */
    OBJ_INTERFACE,    /* `interface X { ... }` */
    OBJ_MODULE,
    OBJ_ENV           /* scope */
} ObjKind;

struct Obj {
    ObjKind kind;
    bool marked;
    bool permanent; /* never collected (interned names, literals) */
    Obj* next;
};

#define IS_OBJ(v) ((v).kind == VAL_OBJ)
#define IS_KIND(v, k) ((v).kind == VAL_OBJ && (v).as.o->kind == (k))
#define AS_STR(v) ((ObjString*)(v).as.o)
#define AS_ARRAY(v) ((ObjArray*)(v).as.o)
#define AS_OBJECT(v) ((ObjObject*)(v).as.o)
#define AS_RANGE(v) ((ObjRange*)(v).as.o)
#define AS_ENUMV(v) ((ObjEnumValue*)(v).as.o)

struct ObjString {
    Obj obj;
    int len;
    uint32_t hash;
    char chars[];
};

/* ordered hash table keyed by strings (compared by content) */
#define VAR_CONST 1
#define VAR_UNINIT 2

typedef struct {
    ObjString* key;
    Value value;
    Type* type;    /* declared type of a variable, NULL when untyped */
    uint8_t flags; /* VAR_* */
} Entry;

typedef struct {
    Entry* entries;
    int count;
    int cap;
    int32_t* index; /* open addressing index into entries, built for larger tables */
    int index_cap;
} Table;

typedef struct {
    Obj obj;
    Value* items;
    int count;
    int cap;
} ObjArray;

typedef struct {
    Obj obj;
    Table fields;
    ObjTypeDef* tag; /* nominal type the object was declared as, or NULL */
} ObjObject;

/* iterates start, start+step, ... while (step > 0 ? v < end : v > end) */
typedef struct {
    Obj obj;
    int64_t start;
    int64_t end;
    int64_t step;
} ObjRange;

typedef struct {
    Type* type; /* NULL = untyped (any) */
    ObjString* name;
} Param;

typedef struct Signature {
    ObjString* name;
    ObjString** generics;
    int ngenerics;
    Param* params;
    int nparams;
    bool variadic;
    bool implicit_result; /* lambda: the value of the body's last expression is returned */
    Type* ret;
    const char* file;
    int line;
} Signature;

typedef struct {
    Obj obj;
    Signature* sig;
    Node* body;
    ObjEnv* closure;
    bool owns_sig; /* a lambda given the parameter types of a callback type; sig is freed with it */
} ObjFunction;

typedef Value (*NativeFn)(VM* vm, int argc, Value* args);

typedef struct {
    Obj obj;
    Signature* sig;
    NativeFn fn;
} ObjNative;

typedef struct {
    Obj obj;
    ObjString* name;
    Obj** items;
    int count;
    int cap;
} ObjOverload;

typedef struct {
    ObjString* name;
    bool is_ctor;           /* declared with parentheses: some(T), none() */
    Type** payload;
    int npayload;
    Value value;            /* associated value (enum string X { A = "a" }) or index */
    ObjEnumValue* singleton; /* shared instance for payload-less variants */
    ObjVariantCtor* ctor;
} Variant;

struct ObjEnum {
    Obj obj;
    ObjString* name;
    ObjString** generics;
    Type** defaults;
    int ngenerics;
    Variant* variants;
    int nvariants;
    Type* value_type;
    ObjEnv* env;
    ObjModule* module; /* module that declared the enum (NULL for the prelude) */
    Table impl_fns;    /* name -> ObjOverload: functions `impl` found for this type, searched by dot calls */
};

struct ObjVariantCtor {
    Obj obj;
    ObjEnum* en;
    int index;
    Signature* sig;
};

struct ObjEnumValue {
    Obj obj;
    ObjEnum* en;
    int variant;
    int count;
    Value payload[];
};

struct ObjTypeDef {
    Obj obj;
    ObjString* name;
    ObjString** generics;
    int ngenerics;
    Type* def;
    ObjEnv* env;
    ObjModule* module; /* module that declared the type (NULL for the prelude) */
    Table impl_fns;    /* name -> ObjOverload: functions `impl` found for this type, searched by dot calls */
};

/* `impl Iface<args> for target` */
typedef struct {
    Type* target;
    ObjEnv* env; /* where target and args resolve */
    Type** args;
    int nargs;
} Impl;

/*
 * An interface is closed: its required functions are fixed by the declaration
 * and cannot grow later (unlike a type, which gains overloads at any time).
 * `impl` checks the requirements once, when it runs.
 */
struct ObjInterface {
    Obj obj;
    ObjString* name;
    ObjString** generics;
    int ngenerics;
    Signature** reqs;
    int nreqs;
    ObjEnv* env;
    ObjModule* module;
    Impl* impls;
    int nimpls;
    int capimpls;
};

struct ObjModule {
    Obj obj;
    ObjString* name;
    ObjString* path;
    ObjEnv* env;
    Table exports; /* exported name -> name of the symbol in env (string value) */
};

struct ObjEnv {
    Obj obj;
    ObjEnv* parent;
    Table vars;
};

/* ---------------------------------------------------------------- api */

uint32_t hash_chars(const char* s, int len);
Obj* alloc_obj(VM* vm, size_t size, ObjKind kind);

ObjString* str_new(VM* vm, const char* s, int len);
ObjString* str_cnew(VM* vm, const char* s);
ObjString* str_intern(VM* vm, const char* s, int len);
ObjString* str_cintern(VM* vm, const char* s);
bool str_equals(ObjString* a, const char* s);

ObjArray* array_new(VM* vm, int cap);
void array_push(ObjArray* a, Value v);
ObjObject* object_new(VM* vm);
ObjRange* range_new(VM* vm, int64_t start, int64_t end, int64_t step);
ObjEnv* env_new(VM* vm, ObjEnv* parent);
ObjOverload* overload_new(VM* vm, ObjString* name);
void overload_add(ObjOverload* set, Obj* fn);
ObjFunction* function_new(VM* vm, Signature* sig, Node* body, ObjEnv* closure);
ObjNative* native_new(VM* vm, Signature* sig, NativeFn fn);
ObjEnumValue* enumv_new(VM* vm, ObjEnum* en, int variant, int count);
ObjModule* module_new(VM* vm, const char* name, const char* path, ObjEnv* parent);
void module_export(ObjModule* m, ObjString* name, ObjString* alias);
bool module_member(ObjModule* m, ObjString* name, Value* out);
int64_t range_length(ObjRange* r);

void table_init(Table* t);
void table_free(Table* t);
Entry* table_find(Table* t, ObjString* key);
Entry* table_find_chars(Table* t, const char* s, int len, uint32_t hash);
Entry* table_set(Table* t, ObjString* key, Value v);
bool table_remove(Table* t, ObjString* key);

bool values_equal(Value a, Value b);
bool is_callable(Value v);
const char* value_type_name(Value v);
void value_format(StrBuf* sb, Value v, bool top, int indent);
void format_float(StrBuf* sb, double d);

#endif
