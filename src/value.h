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
    OBJ_ENV,          /* scope */
    OBJ_TUPLE,        /* fixed size, immutable: tuple(1, "a") */
    OBJ_MAP,          /* std:Map<K, V>, an ObjMap */
    OBJ_SET,          /* std:Set<T>, an ObjMap without values */
    OBJ_NATIVE_TYPE,  /* a type implemented in C (std:Map, std:Set, types:Int8Array, ...), bound by `native type` */
    OBJ_INSTANCE      /* a value of a NativeClass type: ObjInstance followed by the class's own fields */
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
#define AS_TUPLE(v) ((ObjTuple*)(v).as.o)
#define AS_MAP(v) ((ObjMap*)(v).as.o)
#define AS_INSTANCE(v) ((ObjInstance*)(v).as.o)

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

/* tuple(1, "a"): immutable, so it can be a map key or set element */
typedef struct {
    Obj obj;
    int count;
    Value items[];
} ObjTuple;

/*
 * Hash table keyed by values (OBJ_MAP and OBJ_SET; a set leaves `value`
 * unused). Entries keep insertion order; removing one only marks it dead, and
 * dead entries are dropped when the index is rebuilt. `index` is open
 * addressing (linear probing) into entries, -1 = empty slot.
 * Keys must be hashable (value_hash): bool, number, string, enum value, tuple.
 */
typedef struct {
    Value key;
    Value value;
    uint32_t hash;
    bool live;
} MapEntry;

typedef struct {
    Obj obj;
    MapEntry* entries;
    int count; /* used entries, dead ones included */
    int cap;
    int live;  /* entries that are not dead: the size of the map */
    int32_t* index;
    int index_cap; /* power of two, or 0 */
} ObjMap;

typedef enum { NT_MAP, NT_SET, NT_CLASS } NativeTypeKind;

typedef struct ObjNativeType ObjNativeType;
typedef struct ObjInstance ObjInstance;

/*
 * A type a module's C half implements (types:Int8Array, fs:File, ...). The
 * module registers its classes with vm_register_native_types, and its .pp file
 * binds each one with `native type Name`. A value of the type is an
 * ObjInstance: the header below, followed by the fields of the class's own
 * struct (`size` bytes in all). Instances must not hold references to other
 * GC objects (the collector does not look inside them); keep plain C data.
 *
 * A native type cannot be called: values are made by ordinary (native)
 * functions of the module, named in snake_case or flatcase (types:int8array),
 * which build them with instance_new. Every hook is optional.
 */
typedef struct NativeClass {
    const char* name;        /* the name `native type` binds, PascalCase: "Int8Array" */
    size_t size;             /* sizeof the instance struct, ObjInstance header included */
    const char* constructor; /* the function that makes values, for error messages: "types:int8array" */
    void (*free)(ObjInstance* self);                        /* release what the instance owns (not the object) */
    void (*format)(StrBuf* sb, ObjInstance* self);          /* print / str(); default <Name> */
    bool (*equals)(ObjInstance* a, ObjInstance* b);         /* ==, called for two instances of the same class; default identity */
    ObjInstance* (*copy)(VM* vm, ObjInstance* self);        /* copy(x); default returns x itself */
    int64_t (*length)(ObjInstance* self);                   /* #x, and the number of steps of `for` */
    Value (*get)(VM* vm, ObjInstance* self, Value index, int line);            /* x[i], and `for` (with ints 0...#x) */
    void (*set)(VM* vm, ObjInstance* self, Value index, Value v, int line);    /* x[i] = v */
    const void* data; /* anything the class's hooks want to share */
} NativeClass;

/* std:Map / std:Set (or a NativeClass) as a type: matches OBJ_MAP / OBJ_SET (OBJ_INSTANCE) values. It is not callable. */
struct ObjNativeType {
    Obj obj;
    ObjString* name;
    NativeTypeKind which;
    int ngenerics;
    ObjModule* module; /* the module whose `native type` bound it (lib/std.pp) */
    Table impl_fns;    /* name -> ObjOverload: functions `impl` found for this type, searched by dot calls */
    const NativeClass* cls;  /* NT_CLASS only */
    const char* lib_module;  /* NT_CLASS only: the library module that may bind it ("types") */
};

struct ObjInstance {
    Obj obj;
    ObjNativeType* type;
};

/*
 * iterates start, start+step, ... while (step > 0 ? v < end : v > end).
 * start, end and step are numbers of one kind: all VAL_INT or all VAL_FLOAT
 * (range_new converts a mix to floats). Float ranges compute the k-th element
 * as start + k*step, so the step does not accumulate rounding errors.
 */
typedef struct {
    Obj obj;
    Value start;
    Value end;
    Value step;
    bool inclusive; /* written with ...= : end was moved one step past the last element */
} ObjRange;

static inline bool range_is_float(const ObjRange* r) {
    return r->start.kind == VAL_FLOAT;
}

static inline double num_as_double(Value v) {
    return v.kind == VAL_FLOAT ? v.as.f : (double)v.as.i;
}

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
    bool uses_lift;       /* the body names `lift`: calls install a LiftTarget */
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

/*
 * The C half of a mixed library module: its .pp file in lib/ declares
 * `native function name(...) -> T`, and the body comes from a table of these
 * registered with vm_register_native_impls. Terminated by {NULL, NULL}.
 */
typedef struct {
    const char* name;
    NativeFn fn;
} NativeImpl;

typedef struct {
    Obj obj;
    Signature* sig;
    NativeFn fn;
    ObjEnv* env; /* scope its signature's types are resolved in; NULL = builtins.
                    Set for `native function` declared in a .pp module. */
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

/* `impl Iface<args> for target`, `<K, V>impl Index<K, V> for std:Map<K, V>` */
typedef struct {
    Type* target;
    ObjEnv* env; /* where target and args resolve */
    Type** args;
    int nargs;
    ObjString** generics; /* the impl's own generic parameters, bound by the value being matched */
    int ngenerics;
} Impl;

/*
 * An interface is closed: its required functions are fixed by the declaration
 * and cannot grow later (unlike a type, which gains overloads at any time).
 * `impl` checks the requirements once, when it runs. A requirement with a
 * body is optional: a type without its own function gets a copy of the
 * default. An interface that extends others (`impl Equals for Comparator`)
 * makes every implementing type implement them as well.
 */
struct ObjInterface {
    Obj obj;
    ObjString* name;
    ObjString** generics;
    int ngenerics;
    Signature** reqs;
    Node** bodies; /* default implementation per requirement, NULL = none */
    int nreqs;
    Type** parent_types; /* impl Parent<args> for Name, args may use the generics */
    ObjInterface** parents; /* the interfaces this one extends, resolved when declared */
    int nparents;
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
    const NativeImpl* natives; /* C bodies for its `native function`s, or NULL */
    const char* lib_path;      /* the name its C half was registered under ("types"), or NULL */
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
ObjRange* range_new(VM* vm, Value start, Value end, Value step); /* numbers; a mix becomes floats */
Value range_at(ObjRange* r, int64_t k); /* the k-th element */
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

ObjTuple* tuple_new(VM* vm, int count);
ObjInstance* instance_new(VM* vm, ObjNativeType* type); /* zeroed, type->cls->size bytes */
bool value_hash(Value v, uint32_t* out); /* false: not hashable (array, object, function, ...) */
ObjMap* map_new(VM* vm, ObjKind kind);   /* OBJ_MAP or OBJ_SET */
MapEntry* map_find(ObjMap* m, Value key, uint32_t hash);
void map_put(ObjMap* m, Value key, uint32_t hash, Value value);
bool map_remove(ObjMap* m, Value key, uint32_t hash, Value* old);
void map_clear(ObjMap* m);
void map_free(ObjMap* m);

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
