#include "backend.h"
#include "check_int.h"



// The WebAssembly target (--target=wasm): compiles the checker's instances to a .wasm module
// and writes a small JavaScript host (.mjs) and a page (.html) that run it in node or a browser.
//
// Values: int is i64, float f64, bool i32; strings, arrays, records, tuples, enum values,
// ranges, functions and boxes are i32 addresses in linear memory, laid out by their static types
// (every slot is 8 bytes). The host does what is easier in JavaScript: printing (with a table of
// type descriptors, so values print exactly like the interpreter prints them), string
// operations, checked division and indexing, and the heap (see md/TARGETS.md).
//
//   string   [i32 length][bytes]
//   array    [i32 length][i32 capacity][i32 data] -> data: capacity slots
//   record   [i32 shape][i32 -][slots]           shape: [i32 n][n x (i32 name id, i32 type id)]
//   tuple    [slots]
//   enum     [i32 variant][i32 count][slots]
//   range    [i64 start][i64 end][i64 step][i32 inclusive] (ints) / f64 for float ranges
//   function [i32 table slot][i32 scope record][slots: the cells it captured]
//   box      [i32 type id][i32 -][slot]          unknown, unions and interfaces: the value and its type
//   cell     [slot]                              a variable a nested function captured
//
// Every compiled function takes the function value it was called through (its captures) as its
// first parameter, so any function can be called through the table with call_indirect.



// ---------------------------------------------------------------- byte buffers

typedef struct {
    uint8_t* data;
    size_t len;
    size_t cap;
} Bytes;

static void put(Bytes* b, uint8_t x) {
    if (b->len >= b->cap) {
        b->cap = b->cap ? b->cap * 2 : 256;
        b->data = pp_realloc(b->data, b->cap);
    }

    b->data[b->len++] = x;
}

static void put_all(Bytes* b, const void* p, size_t n) {
    for (size_t i = 0; i < n; ++i) {
        put(b, ((const uint8_t*)p)[i]);
    }
}

static void uleb(Bytes* b, uint64_t v) {
    do {
        uint8_t x = v & 0x7F;
        v >>= 7;
        if (v) {
            x |= 0x80;
        }

        put(b, x);
    } while (v);
}

static void sleb(Bytes* b, int64_t v) {
    for (;;) {
        const uint8_t x = v & 0x7F;
        v >>= 7;
        if ((v == 0 && !(x & 0x40)) || (v == -1 && (x & 0x40))) {
            put(b, x);

            return;
        }

        put(b, x | 0x80);
    }
}

static void put_name(Bytes* b, const char* s) {
    uleb(b, strlen(s));
    put_all(b, s, strlen(s));
}

static void section(Bytes* out, uint8_t id, Bytes* body) {
    put(out, id);
    uleb(out, body->len);
    put_all(out, body->data, body->len);
    free(body->data);
    memset(body, 0, sizeof *body);
}



// ---------------------------------------------------------------- wasm vocabulary

enum {
    T_I32 = 0x7F,
    T_I64 = 0x7E,
    T_F64 = 0x7C,
    T_NONE = 0x40
};

enum {
    W_UNREACHABLE = 0x00,
    W_BLOCK = 0x02,
    W_LOOP = 0x03,
    W_IF = 0x04,
    W_ELSE = 0x05,
    W_END = 0x0B,
    W_BR = 0x0C,
    W_BR_IF = 0x0D,
    W_RETURN = 0x0F,
    W_CALL = 0x10,
    W_CALL_INDIRECT = 0x11,
    W_DROP = 0x1A,
    W_SELECT = 0x1B,
    W_LOCAL_GET = 0x20,
    W_LOCAL_SET = 0x21,
    W_LOCAL_TEE = 0x22,
    W_GLOBAL_GET = 0x23,
    W_GLOBAL_SET = 0x24,
    W_I32_LOAD = 0x28,
    W_I64_LOAD = 0x29,
    W_F64_LOAD = 0x2B,
    W_I32_STORE = 0x36,
    W_I64_STORE = 0x37,
    W_F64_STORE = 0x39,
    W_MEMORY_SIZE = 0x3F,
    W_MEMORY_GROW = 0x40,
    W_I32_CONST = 0x41,
    W_I64_CONST = 0x42,
    W_F64_CONST = 0x44,
    W_I32_EQZ = 0x45,
    W_I32_EQ = 0x46,
    W_I32_NE = 0x47,
    W_I32_LE_U = 0x4D,
    W_I64_EQZ = 0x50,
    W_I64_EQ = 0x51,
    W_I64_NE = 0x52,
    W_I64_LT_S = 0x53,
    W_I64_GT_S = 0x55,
    W_I64_LE_S = 0x57,
    W_I64_GE_S = 0x59,
    W_F64_EQ = 0x61,
    W_F64_NE = 0x62,
    W_F64_LT = 0x63,
    W_F64_GT = 0x64,
    W_F64_LE = 0x65,
    W_F64_GE = 0x66,
    W_I32_ADD = 0x6A,
    W_I32_SUB = 0x6B,
    W_I32_MUL = 0x6C,
    W_I32_AND = 0x71,
    W_I32_OR = 0x72,
    W_I32_SHL = 0x74,
    W_I32_SHR_U = 0x76,
    W_I64_ADD = 0x7C,
    W_I64_SUB = 0x7D,
    W_I64_MUL = 0x7E,
    W_F64_NEG = 0x9A,
    W_F64_ADD = 0xA0,
    W_F64_SUB = 0xA1,
    W_F64_MUL = 0xA2,
    W_F64_DIV = 0xA3,
    W_I32_WRAP_I64 = 0xA7,
    W_I64_EXTEND_I32_S = 0xAC,
    W_F64_CONVERT_I64_S = 0xB9
};

// the functions the host provides; the order is their function index
typedef enum {
    H_OUT_I64,      // (type, value): appends a value to the line being printed
    H_OUT_F64,
    H_OUT_I32,
    H_OUT_SPACE,
    H_OUT_END,      // (newline): writes the line
    H_STR_I64,      // (type, value) -> string: str(value)
    H_STR_F64,
    H_STR_I32,
    H_CONCAT,       // (a, b) -> string
    H_STR_EQ,       // (a, b) -> bool
    H_STR_CMP,      // (a, b) -> -1 / 0 / 1
    H_IDIV,         // (a, b) -> a / b, an error for b == 0
    H_IMOD,         // (a, b) -> a % b with the sign of b
    H_FMOD,
    H_IPOW,
    H_FPOW,
    H_ARR_SLOT,     // (array, index) -> address of the slot, an error out of range
    H_ARR_PUSH,     // (array) -> address of a new slot at the end
    H_FIELD,        // (record, name id) -> address of the field's slot
    H_PANIC,        // (message string)
    H_NO_MATCH,     // (line)
    H_FAIL,         // (message string): a runtime error
    H_NATIVE,       // (signature, buffer): a native function the host implements; the arguments
                    // are in the buffer's slots, the result goes into the slot after them
    H_ALLOC,        // (size) -> address of zeroed memory from the host's heap (collected, see JS_HOST)
    H_COUNT
} Host;

typedef struct {
    const char* name;
    const char* params; // i: i32, l: i64, d: f64
    const char* result; // "" or one letter
} HostInfo;

static const HostInfo HOSTS[H_COUNT] = {
    [H_OUT_I64] = {"out_i64", "il", ""},
    [H_OUT_F64] = {"out_f64", "id", ""},
    [H_OUT_I32] = {"out_i32", "ii", ""},
    [H_OUT_SPACE] = {"out_space", "", ""},
    [H_OUT_END] = {"out_end", "i", ""},
    [H_STR_I64] = {"str_i64", "il", "i"},
    [H_STR_F64] = {"str_f64", "id", "i"},
    [H_STR_I32] = {"str_i32", "ii", "i"},
    [H_CONCAT] = {"concat", "ii", "i"},
    [H_STR_EQ] = {"str_eq", "ii", "i"},
    [H_STR_CMP] = {"str_cmp", "ii", "i"},
    [H_IDIV] = {"idiv", "ll", "l"},
    [H_IMOD] = {"imod", "ll", "l"},
    [H_FMOD] = {"fmod", "dd", "d"},
    [H_IPOW] = {"ipow", "ll", "l"},
    [H_FPOW] = {"fpow", "dd", "d"},
    [H_ARR_SLOT] = {"arr_slot", "il", "i"},
    [H_ARR_PUSH] = {"arr_push", "i", "i"},
    [H_FIELD] = {"field", "ii", "i"},
    [H_PANIC] = {"panic", "i", ""},
    [H_NO_MATCH] = {"no_match", "i", ""},
    [H_FAIL] = {"fail_msg", "i", ""},
    [H_NATIVE] = {"native", "ii", ""},
    [H_ALLOC] = {"gc_alloc", "i", "i"},
};



// ---------------------------------------------------------------- the module being built

typedef struct {
    Bytes params; // valtypes
    Bytes results;
} FuncType;

typedef struct {
    bool from_local; // a cell in a local of the function that creates the closure, else one of its captures
    int index;
    SType* type;     // the variable's type
} WCapRef;

typedef struct {
    CInstance* inst;    // NULL: a block, a defer or a generated function
    int index;          // wasm function index
    int type;
    int table_slot;     // in the funcref table, -1: not used as a value
    bool done;
    bool nested;        // a lambda or local function: compiled where its closure is made
    Bytes body;
    WCapRef* caps;      // what its closures capture, from where they are made
    int ncaps;
} WFunc;

typedef struct {
    ObjString* name;
    int index;      // wasm global
    uint8_t vt;
    CModule* module;
} WGlobal;

typedef struct {
    CModule* module;
    int func;       // its top level code
    int flag;       // global: it ran
} WModule;

typedef struct WasmGen {
    VM* vm;
    Checker* ch;
    FuncType* types;
    int ntypes;
    int captypes;
    WFunc** funcs;
    int nfuncs;
    int capfuncs;
    int* table;       // the funcref table: functions used as values (call_indirect)
    int ntable;
    int captable;
    int* boxed;       // the type ids of the values that were boxed
    int nboxed;
    int capboxed;
    SType** tests;    // type tests (type id) -> i32: does a boxed value of that type fit tests[i]
    WFunc** test_funcs;
    int ntests;
    int captests;
    int g_exit;       // globals: how the running block leaves its scope call (ScopeExit), and its record
    int g_exit_rec;
    int g_ret[3];     // ... the value of a return out of a block: i32, i64, f64
    int g_scope;      // the innermost running scope call's record: [i32 defer][i32 previous record]
    StrBuf sigjs;     // the signatures of the natives the host runs (H_NATIVE), as JavaScript
    int nsigs;
    WGlobal* globals;
    int nglobals;
    int capglobals;
    WModule* modules;
    int nmodules;
    int capmodules;
    Bytes data;       // the data segment, starting at address DATA_BASE
    ObjString** strs; // literals already in the data segment, and their addresses
    int* str_addr;
    int nstrs;
    int capstrs;
    ObjString** names;// field name ids
    int nnames;
    int capnames;
    StrBuf typejs;    // the type descriptors, as JavaScript
    SType** tdescs;
    int ntdescs;
    int captdescs;
    int heap_global;
    jmp_buf jb;
    char error[512];
    const char* error_file;
    int error_line;
} WasmGen;

#define DATA_BASE 16

static WasmGen* G;



static PP_NORETURN void wfail(int line, const char* file, const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(G->error, sizeof G->error, fmt, ap);
    va_end(ap);
    G->error_line = line;
    G->error_file = file;
    longjmp(G->jb, 1);
}

static int func_type(const char* params, const char* result) {
    Bytes p = {0}, r = {0};
    for (const char* s = params; *s; ++s) {
        put(&p, *s == 'i' ? T_I32 : *s == 'l' ? T_I64 : T_F64);
    }

    for (const char* s = result; *s; ++s) {
        put(&r, *s == 'i' ? T_I32 : *s == 'l' ? T_I64 : T_F64);
    }

    for (int i = 0; i < G->ntypes; ++i) {
        const FuncType* t = &G->types[i];
        if (t->params.len == p.len && t->results.len == r.len && (!p.len || memcmp(t->params.data, p.data, p.len) == 0) &&
            (!r.len || memcmp(t->results.data, r.data, r.len) == 0)) {
            free(p.data);
            free(r.data);

            return i;
        }
    }

    GROW(G->types, G->ntypes, G->captypes);
    G->types[G->ntypes].params = p;
    G->types[G->ntypes].results = r;

    return G->ntypes++;
}

// Map<K, V> / Set<T>
static bool is_collection_type(const SType* t) {
    return t && t->kind == ST_NATIVE && t->decl && (t->decl->which == NT_MAP || t->decl->which == NT_SET);
}

// the value type of a static type, 0 for void / never; an error for types this target has no representation for
static uint8_t valtype(const SType* t, int line, const char* file) {
    switch (t->kind) {
        case ST_INT: return T_I64;
        case ST_FLOAT: return T_F64;
        case ST_BOOL: return T_I32;
        case ST_STRING:
        case ST_ARRAY:
        case ST_RECORD:
        case ST_TUPLE:
        case ST_ENUM:
        case ST_RANGE:
        case ST_FUNC:
        case ST_BLOCK:
        case ST_UNKNOWN:
        case ST_UNION:
        case ST_IFACE:
        case ST_FUNCTION:
        case ST_OVERLOAD: return T_I32;
        case ST_VOID:
        case ST_NEVER: return 0;
        default:
            if (t->kind == ST_NATIVE && t->decl && t->decl->which == NT_GENERATOR) {
                return T_I32; // the address of [step][state][value][frame]
            }

            if (is_collection_type(t)) {
                return T_I32; // the address of a Map / Set (see JS_HOST)
            }

            wfail(line, file, "the wasm target has no representation for values of type %s yet", st_name(t));
    }
}

// can this target make values of type t (valtype would not fail): what a box can hold
static bool representable(const SType* t) {
    switch (t->kind) {
        case ST_RANGE: return !t->elem || t->elem->kind == ST_INT; // int ranges only, for now
        case ST_INT:
        case ST_FLOAT:
        case ST_BOOL:
        case ST_STRING:
        case ST_RECORD:
        case ST_ENUM:
        case ST_FUNC:
        case ST_BLOCK:
        case ST_UNKNOWN:
        case ST_UNION:
        case ST_IFACE:
        case ST_FUNCTION:
        case ST_OVERLOAD:
        case ST_VOID:
        case ST_NEVER: return true;
        case ST_ARRAY: return !t->elem || representable(t->elem);
        case ST_TUPLE:
            for (int i = 0; i < t->nargs; ++i) {
                if (!representable(t->args[i])) {
                    return false;
                }
            }

            return true;
        default:
            return t->kind == ST_NATIVE && t->decl && (t->decl->which == NT_GENERATOR || is_collection_type(t));
    }
}

static char letter(uint8_t vt) {
    return vt == T_I64 ? 'l' : vt == T_F64 ? 'd' : 'i';
}

static int str_addr(ObjString* s) {
    for (int i = 0; i < G->nstrs; ++i) {
        if (G->strs[i] == s || (G->strs[i]->len == s->len && memcmp(G->strs[i]->chars, s->chars, (size_t)s->len) == 0)) {
            return G->str_addr[i];
        }
    }

    while (G->data.len % 8) {
        put(&G->data, 0);
    }

    const int addr = DATA_BASE + (int)G->data.len;
    const uint32_t len = (uint32_t)s->len;
    put_all(&G->data, &len, 4);
    put_all(&G->data, s->chars, (size_t)s->len);
    if (G->nstrs >= G->capstrs) {
        G->capstrs = G->capstrs ? G->capstrs * 2 : 32;
        G->strs = pp_realloc(G->strs, sizeof(ObjString*) * (size_t)G->capstrs);
        G->str_addr = pp_realloc(G->str_addr, sizeof(int) * (size_t)G->capstrs);
    }

    G->strs[G->nstrs] = s;
    G->str_addr[G->nstrs] = addr;
    G->nstrs++;

    return addr;
}

static int name_id(ObjString* s) {
    for (int i = 0; i < G->nnames; ++i) {
        if (G->names[i] == s || str_equals(G->names[i], s->chars)) {
            return i;
        }
    }

    GROW(G->names, G->nnames, G->capnames);
    G->names[G->nnames] = s;

    return G->nnames++;
}

static void js_string(StrBuf* sb, const char* s) {
    sb_putc(sb, '"');
    for (; *s; ++s) {
        if (*s == '"' || *s == '\\') {
            sb_putc(sb, '\\');
        }

        sb_putc(sb, *s);
    }

    sb_putc(sb, '"');
}

// a type argument that says what a collection holds (map() is a Map<never, never> until used)
static bool known_arg(const SType* t) {
    return t && t->kind != ST_NEVER && t->kind != ST_DYN && !st_has_param(t);
}

// the id of a type descriptor the host formats values with
static int type_desc(SType* t) {
    for (int i = 0; i < G->ntdescs; ++i) {
        if (st_equal(G->tdescs[i], t)) {
            return i;
        }
    }

    const int id = G->ntdescs;
    GROW(G->tdescs, G->ntdescs, G->captdescs);
    G->tdescs[G->ntdescs++] = t;
    // the descriptor's text: children first need ids, so build it into its own buffer
    StrBuf d;
    sb_init(&d);
    switch (t->kind) {
        case ST_INT: sb_puts(&d, "{k:'int'}"); break;
        case ST_FLOAT: sb_puts(&d, "{k:'float'}"); break;
        case ST_BOOL: sb_puts(&d, "{k:'bool'}"); break;
        case ST_STRING: sb_puts(&d, "{k:'string'}"); break;
        case ST_VOID: sb_puts(&d, "{k:'void'}"); break;
        case ST_RANGE: sb_printf(&d, "{k:'range',f:%s}", t->elem && t->elem->kind == ST_FLOAT ? "true" : "false"); break;
        case ST_ARRAY: sb_printf(&d, "{k:'array',e:%d}", type_desc(t->elem)); break;
        case ST_RECORD:
            if (t->decl) {
                sb_puts(&d, "{k:'record',n:");
                js_string(&d, t->decl->name->chars);
                sb_puts(&d, "}");
            } else {
                sb_puts(&d, "{k:'record'}");
            }

            break;
        case ST_TUPLE: {
            sb_puts(&d, "{k:'tuple',e:[");
            for (int i = 0; i < t->nargs; ++i) {
                sb_printf(&d, i ? ",%d" : "%d", type_desc(t->args[i]));
            }

            sb_puts(&d, "]}");
            break;
        }
        case ST_ENUM: {
            sb_puts(&d, "{k:'enum',n:");
            js_string(&d, t->decl->name->chars);
            sb_puts(&d, ",v:[");
            for (int i = 0; i < t->decl->nvariants; ++i) {
                const CVariant* v = &t->decl->variants[i];
                sb_puts(&d, i ? ",[" : "[");
                js_string(&d, v->name->chars);
                sb_printf(&d, ",%s,[", v->is_ctor ? "true" : "false");
                for (int k = 0; k < v->npayload; ++k) {
                    SType* pt = variant_payload(G->ch, t, i, k);
                    sb_printf(&d, k ? ",%d" : "%d", pt->kind == ST_NEVER ? type_desc(st_prim(ST_VOID)) : type_desc(pt));
                }

                sb_puts(&d, "]]");
            }

            sb_puts(&d, "]}");
            break;
        }
        case ST_UNKNOWN:
        case ST_UNION:
        case ST_IFACE: sb_puts(&d, "{k:'box'}"); break;
        case ST_FUNC: {
            sb_puts(&d, "{k:'function',p:[");
            for (int i = 0; i < t->nargs; ++i) {
                sb_printf(&d, i ? ",%d" : "%d", type_desc(t->args[i]));
            }

            sb_printf(&d, "],r:%d}", t->elem && t->elem->kind != ST_VOID ? type_desc(t->elem) : -1);
            break;
        }
        case ST_BLOCK:
        case ST_FUNCTION:
        case ST_OVERLOAD: sb_puts(&d, "{k:'function',p:[],r:-1}"); break;
        default:
            if (is_collection_type(t)) {
                // the key / item and value types: -1 when the static type does not say (map() -> Map)
                const bool map = t->decl->which == NT_MAP;
                const int kt = t->nargs > 0 && known_arg(t->args[0]) ? type_desc(t->args[0]) : -1;
                const int vt = !map ? -2 : t->nargs > 1 && known_arg(t->args[1]) ? type_desc(t->args[1]) : -1;
                sb_printf(&d, "{k:'%s',kt:%d,vt:%d}", map ? "map" : "set", kt, vt);
                break;
            }

            sb_printf(&d, "{k:'other',n:'%s'}", st_name(t));
            break;
    }

    sb_printf(&G->typejs, "T[%d]=%s;\n", id, d.data);
    sb_free(&d);

    return id;
}

// ---------------------------------------------------------------- functions

// a function of the module; its body comes later
static WFunc* wfunc_add(int type) {
    WFunc* f = pp_calloc(1, sizeof(WFunc));
    f->index = H_COUNT + 1 + G->nfuncs; // after the host functions and alloc
    f->type = type;
    f->table_slot = -1;
    GROW(G->funcs, G->nfuncs, G->capfuncs);
    G->funcs[G->nfuncs++] = f;

    return f;
}

static bool is_nested_fn(const CFunc* f) {
    if (f->lambda) {
        return true;
    }

    const CScope* s = f->closure;

    return s && s->inst && s->inst->fn; // declared in a function's body, not at a module's top level
}

static WFunc* wfunc_for(CInstance* inst) {
    for (int i = 0; i < G->nfuncs; ++i) {
        if (G->funcs[i]->inst == inst) {
            return G->funcs[i];
        }
    }

    // the signature: the function value it was called through, the parameters, the result
    char params[64] = {0};
    char result[2] = {0};
    const char* file = inst->fn && inst->fn->sig ? inst->fn->sig->file : NULL;
    const int line = inst->fn && inst->fn->sig ? inst->fn->sig->line : 0;
    int np = 0;
    if (inst->fn) {
        params[np++] = 'i';
    }

    for (int i = 0; i < inst->nparams && np < 60; ++i) {
        params[np++] = letter(valtype(inst->params[i], line, file));
    }

    const uint8_t rt = inst->fn ? valtype(inst->ret, line, file) : 0;
    if (rt) {
        result[0] = letter(rt);
    }

    WFunc* f = wfunc_add(func_type(params, result));
    f->inst = inst;
    f->nested = inst->fn && is_nested_fn(inst->fn);

    return f;
}

static int table_slot(WFunc* f) {
    if (f->table_slot < 0) {
        GROW(G->table, G->ntable, G->captable);
        f->table_slot = G->ntable;
        G->table[G->ntable++] = f->index;
    }

    return f->table_slot;
}

// the wasm type of calling a value of function type t (ST_FUNC, or a scope's ST_BLOCK)
static int call_type(const SType* t, int line, const char* file) {
    char params[64] = {0};
    char result[2] = {0};
    int np = 0;
    params[np++] = 'i';
    if (t->kind == ST_BLOCK) {
        if (t->elem && valtype(t->elem, line, file)) {
            params[np++] = letter(valtype(t->elem, line, file));
        }

        const SType* r = !t->variadic && t->nargs ? t->args[0] : NULL;
        if (r && valtype(r, line, file)) {
            result[0] = letter(valtype(r, line, file));
        }
    } else {
        for (int i = 0; i < t->nargs && np < 60; ++i) {
            params[np++] = letter(valtype(t->args[i], line, file));
        }

        if (t->elem && valtype(t->elem, line, file)) {
            result[0] = letter(valtype(t->elem, line, file));
        }
    }

    return func_type(params, result);
}



// ---------------------------------------------------------------- one function's code

typedef struct {
    ObjString* name;
    int index;
    uint8_t vt;
    int depth;
    bool boxed;     // the local holds a cell (a nested function captured the variable)
    SType* type;
} WLocal;

typedef struct {
    ObjString* name;
    WCapRef ref;
} WCap;

// what is being compiled: a function, or the body of a scope call's block / defer
typedef enum { WK_FUNCTION, WK_BLOCK, WK_CALLBACK, WK_DEFER } WKind;

typedef struct {
    int exit_depth;  // the label depth of the block a break leaves
    int cont_depth;  // ... and of the loop a continue restarts
} WLoop;

typedef struct WCtx {
    CInstance* inst;
    WFunc* wf;
    CModule* module;
    Bytes* b;
    WLocal* locals;
    int nlocals;
    int caplocals;
    uint8_t* local_types; // all locals beyond the parameters, by index
    int nlocal_types;
    int caplocal_types;
    int nparams;
    int depth;            // scope depth for names
    int labels;           // how many blocks / loops / ifs are open
    WLoop loops[64];
    int nloops;
    bool top;             // a module's top level: depth 0 declarations are globals
    const char* file;
    struct WCtx* outer;   // a nested function: the function its closure is made in
    WCap* caps;
    int ncaps;
    int capcaps;
    NameList boxnames;    // variables nested functions use: they live in cells
    WKind kind;
    uint8_t result_vt;    // the wasm result of the function being compiled
    ObjString* scope;     // a block: the name of its scope
    int closure_local;    // the local with the function value (its captures): 0, or a generator's own
    // a generator's step function (see compile_generator)
    bool gen;
    int gen_resume;       // the local: the pause being resumed at, 0 while running normally
    int gen_exit;         // the label of the block a pause leaves
    SType* gen_elem;      // the type of its values
    Node** pauses;        // its emits, in the order of the source: pause k is pauses[k - 1]
    int npauses;
    int cappauses;
} WCtx;

static void wexpr(WCtx* w, Node* n);
static void wstmt(WCtx* w, Node* n);
static int module_init(CModule* m);

static PP_NORETURN void unsupported(WCtx* w, Node* n, const char* what) {
    wfail(n ? n->line : 0, w->file, "%s is not supported by the wasm target yet", what);
}

static void op(WCtx* w, uint8_t o) {
    put(w->b, o);
}

static void op_u(WCtx* w, uint8_t o, uint64_t v) {
    put(w->b, o);
    uleb(w->b, v);
}

static void i32c(WCtx* w, int32_t v) {
    put(w->b, W_I32_CONST);
    sleb(w->b, v);
}

static void mem(WCtx* w, uint8_t o, int align, int offset) {
    put(w->b, o);
    uleb(w->b, (uint64_t)align);
    uleb(w->b, (uint64_t)offset);
}

static void host(WCtx* w, Host h) {
    op_u(w, W_CALL, h);
}

static void open_block(WCtx* w, uint8_t o, uint8_t bt) {
    put(w->b, o);
    put(w->b, bt ? bt : T_NONE);
    w->labels++;
}

static void close_block(WCtx* w) {
    op(w, W_END);
    w->labels--;
}

static NodeInfo* winfo(WCtx* w, Node* n) {
    return instance_info(w->inst, n);
}

static SType* wtype(WCtx* w, Node* n) {
    const NodeInfo* i = winfo(w, n);
    if (!i || !i->type || st_is_dyn(i->type)) {
        wfail(n->line, w->file, "this expression has no static type");
    }

    return i->type;
}

static int new_local(WCtx* w, uint8_t vt) {
    if (w->nlocal_types >= w->caplocal_types) {
        w->caplocal_types = w->caplocal_types ? w->caplocal_types * 2 : 16;
        w->local_types = pp_realloc(w->local_types, (size_t)w->caplocal_types);
    }

    w->local_types[w->nlocal_types] = vt;

    return w->nparams + w->nlocal_types++;
}

static int add_named(WCtx* w, ObjString* name, uint8_t vt) {
    const int idx = new_local(w, vt);
    GROW(w->locals, w->nlocals, w->caplocals);
    w->locals[w->nlocals].name = name;
    w->locals[w->nlocals].index = idx;
    w->locals[w->nlocals].vt = vt;
    w->locals[w->nlocals].depth = w->depth;
    w->locals[w->nlocals].boxed = false;
    w->locals[w->nlocals].type = NULL;
    w->nlocals++;

    return idx;
}

static void begin(WCtx* w) {
    w->depth++;
}

static void finish(WCtx* w) {
    w->depth--;
    while (w->nlocals > 0 && w->locals[w->nlocals - 1].depth > w->depth) {
        w->nlocals--;
    }
}

static const WGlobal* find_global(CModule* m, ObjString* name) {
    for (int i = G->nglobals - 1; i >= 0; --i) {
        if (G->globals[i].module == m && str_equals(G->globals[i].name, name->chars)) {
            return &G->globals[i];
        }
    }

    return NULL;
}

static const WGlobal* new_global(CModule* m, ObjString* name, uint8_t vt) {
    GROW(G->globals, G->nglobals, G->capglobals);
    WGlobal* g = &G->globals[G->nglobals];
    g->name = name;
    g->index = G->nglobals + 1; // global 0 is the heap pointer
    g->vt = vt;
    g->module = m;
    G->nglobals++;

    return g;
}

static void alloc(WCtx* w, int size);
static void load_slot(WCtx* w, const SType* t, int offset, int line);
static void store_slot(WCtx* w, const SType* t, int offset, int line);

// the value on the stack goes into a new variable; one that nested functions use goes into a cell
static void declare(WCtx* w, ObjString* name, SType* t, int line) {
    const uint8_t vt = valtype(t, line, w->file);
    if (!vt) {
        op(w, W_DROP);

        return;
    }

    if (w->top && w->depth == 0) {
        op_u(w, W_GLOBAL_SET, new_global(w->module, name, vt)->index);

        return;
    }

    if (!names_has(&w->boxnames, name)) {
        const int idx = add_named(w, name, vt);
        w->locals[w->nlocals - 1].type = t;
        op_u(w, W_LOCAL_SET, idx);

        return;
    }

    const int v = new_local(w, vt);
    op_u(w, W_LOCAL_SET, v);
    const int cell = add_named(w, name, T_I32);
    w->locals[w->nlocals - 1].boxed = true;
    w->locals[w->nlocals - 1].type = t;
    alloc(w, 8);
    op_u(w, W_LOCAL_TEE, cell);
    op_u(w, W_LOCAL_GET, v);
    store_slot(w, t, 0, line);
}

static const WLocal* find_local(WCtx* w, ObjString* name) {
    for (int i = w->nlocals - 1; i >= 0; --i) {
        if (str_equals(w->locals[i].name, name->chars)) {
            return &w->locals[i];
        }
    }

    return NULL;
}

// the index of name among w's captures, adding it; -1 when no enclosing function has it
static int find_capture(WCtx* w, ObjString* name) {
    for (int i = 0; i < w->ncaps; ++i) {
        if (str_equals(w->caps[i].name, name->chars)) {
            return i;
        }
    }

    if (!w->outer) {
        return -1;
    }

    WCap cap;
    cap.name = name;
    const WLocal* l = find_local(w->outer, name);
    if (l) {
        if (!l->boxed) {
            return -1;
        }

        cap.ref.from_local = true;
        cap.ref.index = l->index;
        cap.ref.type = l->type;
    } else {
        const int k = find_capture(w->outer, name);
        if (k < 0) {
            return -1;
        }

        cap.ref.from_local = false;
        cap.ref.index = k;
        cap.ref.type = w->outer->caps[k].ref.type;
    }

    GROW(w->caps, w->ncaps, w->capcaps);
    w->caps[w->ncaps] = cap;

    return w->ncaps++;
}

// the address of capture k's cell: the function value is parameter 0
static void cap_cell(WCtx* w, int k) {
    op_u(w, W_LOCAL_GET, (uint64_t)w->closure_local);
    mem(w, W_I32_LOAD, 2, 8 + 8 * k);
}

static bool load_var(WCtx* w, ObjString* name) {
    const WLocal* l = find_local(w, name);
    if (l) {
        op_u(w, W_LOCAL_GET, l->index);
        if (l->boxed) {
            load_slot(w, l->type, 0, 0);
        }

        return true;
    }

    const int k = find_capture(w, name);
    if (k >= 0) {
        cap_cell(w, k);
        load_slot(w, w->caps[k].ref.type, 0, 0);

        return true;
    }

    const WGlobal* g = find_global(w->module, name);
    if (g) {
        op_u(w, W_GLOBAL_GET, g->index);

        return true;
    }

    return false;
}

static void store_var(WCtx* w, ObjString* name, int line) {
    const WLocal* l = find_local(w, name);
    if (l && !l->boxed) {
        op_u(w, W_LOCAL_SET, l->index);

        return;
    }

    const int k = l ? -1 : find_capture(w, name);
    if (l || k >= 0) {
        SType* t = l ? l->type : w->caps[k].ref.type;
        const int v = new_local(w, valtype(t, line, w->file));
        op_u(w, W_LOCAL_SET, v);
        if (l) {
            op_u(w, W_LOCAL_GET, l->index);
        } else {
            cap_cell(w, k);
        }

        op_u(w, W_LOCAL_GET, v);
        store_slot(w, t, 0, line);

        return;
    }

    const WGlobal* g = find_global(w->module, name);
    if (!g) {
        wfail(line, w->file, "the wasm target cannot find the variable '%s'", name->chars);
    }

    op_u(w, W_GLOBAL_SET, g->index);
}

// loads / stores a slot (address on the stack) of a value of type t
static void load_slot(WCtx* w, const SType* t, int offset, int line) {
    switch (valtype(t, line, w->file)) {
        case T_I64: mem(w, W_I64_LOAD, 3, offset); return;
        case T_F64: mem(w, W_F64_LOAD, 3, offset); return;
        case T_I32: mem(w, W_I32_LOAD, 2, offset); return;
        default: op(w, W_DROP); return;
    }
}

static void store_slot(WCtx* w, const SType* t, int offset, int line) {
    switch (valtype(t, line, w->file)) {
        case T_I64: mem(w, W_I64_STORE, 3, offset); return;
        case T_F64: mem(w, W_F64_STORE, 3, offset); return;
        case T_I32: mem(w, W_I32_STORE, 2, offset); return;
        default: return;
    }
}

static int alloc_func(void) {
    return H_COUNT + 0; // set up in wasm_compile: the first defined function
}

static void alloc(WCtx* w, int size) {
    i32c(w, size);
    op_u(w, W_CALL, alloc_func());
}

static bool is_boxed(const SType* t) {
    return t && (t->kind == ST_UNKNOWN || t->kind == ST_UNION || t->kind == ST_IFACE);
}

// the value on the stack, of the static type t, into a box with its type id
static void box(WCtx* w, SType* t, int line) {
    const uint8_t vt = valtype(t, line, w->file);
    if (!vt) {
        wfail(line, w->file, "a value of type %s cannot be stored", st_name(t));
    }

    const int v = new_local(w, vt);
    op_u(w, W_LOCAL_SET, v);
    const int p = new_local(w, T_I32);
    const int id = type_desc(t);
    bool seen = false;
    for (int i = 0; i < G->nboxed; ++i) {
        seen = seen || G->boxed[i] == id;
    }

    if (!seen) {
        GROW(G->boxed, G->nboxed, G->capboxed);
        G->boxed[G->nboxed++] = id;
    }

    alloc(w, 16);
    op_u(w, W_LOCAL_TEE, p);
    i32c(w, id);
    mem(w, W_I32_STORE, 2, 0);
    op_u(w, W_LOCAL_GET, p);
    op_u(w, W_LOCAL_GET, v);
    store_slot(w, t, 8, line);
    op_u(w, W_LOCAL_GET, p);
}

static void convert(WCtx* w, const SType* from, const SType* to);

// an array (on the stack) whose elements must change representation (boxed, or int to float): a
// new array with the converted elements, as the interpreter's values would be after the call
static void convert_array(WCtx* w, const SType* from, const SType* to) {
    const int src = new_local(w, T_I32);
    const int dst = new_local(w, T_I32);
    const int i = new_local(w, T_I32);
    const int n = new_local(w, T_I32);
    op_u(w, W_LOCAL_TEE, (uint64_t)src);
    mem(w, W_I32_LOAD, 2, 0);
    op_u(w, W_LOCAL_SET, (uint64_t)n);
    alloc(w, 16);
    op_u(w, W_LOCAL_TEE, (uint64_t)dst);
    op_u(w, W_LOCAL_GET, (uint64_t)n);
    mem(w, W_I32_STORE, 2, 0);
    op_u(w, W_LOCAL_GET, (uint64_t)dst);
    op_u(w, W_LOCAL_GET, (uint64_t)n);
    i32c(w, 1);
    op_u(w, W_LOCAL_GET, (uint64_t)n);
    op(w, W_SELECT); // capacity: n, at least 1
    mem(w, W_I32_STORE, 2, 4);
    op_u(w, W_LOCAL_GET, (uint64_t)dst);
    op_u(w, W_LOCAL_GET, (uint64_t)n);
    i32c(w, 3);
    op(w, W_I32_SHL);
    i32c(w, 8);
    op(w, W_I32_ADD);
    op_u(w, W_CALL, (uint64_t)alloc_func());
    mem(w, W_I32_STORE, 2, 8);
    i32c(w, 0);
    op_u(w, W_LOCAL_SET, (uint64_t)i);
    open_block(w, W_BLOCK, 0);
    const int exit = w->labels;
    open_block(w, W_LOOP, 0);
    const int top = w->labels;
    op_u(w, W_LOCAL_GET, (uint64_t)i);
    op_u(w, W_LOCAL_GET, (uint64_t)n);
    op(w, W_I32_EQ);
    op_u(w, W_BR_IF, (uint64_t)(w->labels - exit));
    // dst.data + 8i <- convert(src.data + 8i)
    op_u(w, W_LOCAL_GET, (uint64_t)dst);
    mem(w, W_I32_LOAD, 2, 8);
    op_u(w, W_LOCAL_GET, (uint64_t)i);
    i32c(w, 3);
    op(w, W_I32_SHL);
    op(w, W_I32_ADD);
    op_u(w, W_LOCAL_GET, (uint64_t)src);
    mem(w, W_I32_LOAD, 2, 8);
    op_u(w, W_LOCAL_GET, (uint64_t)i);
    i32c(w, 3);
    op(w, W_I32_SHL);
    op(w, W_I32_ADD);
    load_slot(w, from->elem, 0, 0);
    convert(w, from->elem, to->elem);
    store_slot(w, to->elem, 0, 0);
    op_u(w, W_LOCAL_GET, (uint64_t)i);
    i32c(w, 1);
    op(w, W_I32_ADD);
    op_u(w, W_LOCAL_SET, (uint64_t)i);
    op_u(w, W_BR, (uint64_t)(w->labels - top));
    close_block(w);
    close_block(w);
    op_u(w, W_LOCAL_GET, (uint64_t)dst);
}

static bool needs_convert(const SType* from, const SType* to) {
    if (!from || !to) {
        return false;
    }

    if (from->kind == ST_INT && to->kind == ST_FLOAT) {
        return true;
    }

    if (is_boxed(to)) {
        return !is_boxed(from) && from->kind != ST_NEVER;
    }

    return from->kind == ST_ARRAY && to->kind == ST_ARRAY && from->elem && to->elem && needs_convert(from->elem, to->elem);
}

static void convert(WCtx* w, const SType* from, const SType* to) {
    if (!needs_convert(from, to)) {
        return;
    }

    if (from->kind == ST_INT && to->kind == ST_FLOAT) {
        op(w, W_F64_CONVERT_I64_S);
    } else if (is_boxed(to)) {
        box(w, (SType*)from, 0);
    } else {
        convert_array(w, from, to);
    }
}

// the function that tests a type id (on the stack) against t: i32 1 when a value of it fits t
static int type_test(SType* t) {
    for (int i = 0; i < G->ntests; ++i) {
        if (st_equal(G->tests[i], t)) {
            return G->test_funcs[i]->index;
        }
    }

    if (G->ntests >= G->captests) {
        G->captests = G->captests ? G->captests * 2 : 8;
        G->tests = pp_realloc(G->tests, sizeof(SType*) * (size_t)G->captests);
        G->test_funcs = pp_realloc(G->test_funcs, sizeof(WFunc*) * (size_t)G->captests);
    }

    WFunc* f = wfunc_add(func_type("i", "i"));
    f->done = true; // the body is made last, when every boxed type is known
    G->tests[G->ntests] = t;
    G->test_funcs[G->ntests] = f;
    G->ntests++;

    return f->index;
}

// a boxed value (its address on the stack) unboxed to the type t it was tested to fit
static void unbox(WCtx* w, SType* t, int line) {
    if (is_boxed(t)) {
        return;
    }

    if (t->kind == ST_FLOAT) {
        // an int fits a float: it is converted
        const int b = new_local(w, T_I32);
        op_u(w, W_LOCAL_TEE, b);
        mem(w, W_I32_LOAD, 2, 0);
        i32c(w, type_desc(st_prim(ST_INT)));
        op(w, W_I32_EQ);
        open_block(w, W_IF, T_F64);
        op_u(w, W_LOCAL_GET, b);
        mem(w, W_I64_LOAD, 3, 8);
        op(w, W_F64_CONVERT_I64_S);
        op(w, W_ELSE);
        op_u(w, W_LOCAL_GET, b);
        mem(w, W_F64_LOAD, 3, 8);
        close_block(w);

        return;
    }

    load_slot(w, t, 8, line);
}

static void record(WCtx* w, Node* n, SType* want);

static void wexpr_to(WCtx* w, Node* n, const SType* to) {
    if (n->kind == N_OBJECT && to && to->kind == ST_RECORD) {
        record(w, n, (SType*)to);

        return;
    }

    wexpr(w, n);
    convert(w, wtype(w, n), to);
}



// ---------------------------------------------------------------- calls

static Host host_of(uint8_t vt, Host i64, Host f64, Host i32) {
    return vt == T_I64 ? i64 : vt == T_F64 ? f64 : i32;
}

// a value (on the stack, of type t) handed to one of the host's (type, value) functions
static void to_host(WCtx* w, SType* t, Host i64, Host f64, Host i32, int line) {
    const uint8_t vt = valtype(t, line, w->file);
    // the host takes the descriptor first: keep the value in a local while pushing it
    const int tmp = new_local(w, vt);
    op_u(w, W_LOCAL_SET, tmp);
    i32c(w, type_desc(t));
    op_u(w, W_LOCAL_GET, tmp);
    host(w, host_of(vt, i64, f64, i32));
}

static void push_args(WCtx* w, Node* recv, NodeList* args, CInstance* target) {
    int i = 0;
    if (target && target->fn) {
        i32c(w, 0); // called directly: no function value
    }

    if (recv) {
        wexpr_to(w, recv, target && target->nparams ? target->params[0] : NULL);
        i++;
    }

    for (int k = 0; k < args->count; ++k, ++i) {
        wexpr_to(w, args->items[k], target && i < target->nparams ? target->params[i] : NULL);
    }
}

static Node* arg_at(Node* recv, NodeList* args, int i) {
    if (recv) {
        return i == 0 ? recv : args->items[i - 1];
    }

    return args->items[i];
}

static bool float_cop(int cop) {
    return (cop >= COP_ADD_F && cop <= COP_POW_F) || (cop >= COP_EQ_F && cop <= COP_GE_F);
}

// a built-in operation; the operands are pushed by `operands`
static void cop(WCtx* w, int c, int line) {
    static const struct {
        int cop;
        int op;
        Host host;
    } table[] = {
        {COP_ADD_I, W_I64_ADD, H_COUNT}, {COP_SUB_I, W_I64_SUB, H_COUNT}, {COP_MUL_I, W_I64_MUL, H_COUNT},
        {COP_DIV_I, -1, H_IDIV},          {COP_MOD_I, -1, H_IMOD},          {COP_POW_I, -1, H_IPOW},
        {COP_ADD_F, W_F64_ADD, H_COUNT}, {COP_SUB_F, W_F64_SUB, H_COUNT}, {COP_MUL_F, W_F64_MUL, H_COUNT},
        {COP_DIV_F, W_F64_DIV, H_COUNT}, {COP_MOD_F, -1, H_FMOD},          {COP_POW_F, -1, H_FPOW},
        {COP_EQ_I, W_I64_EQ, H_COUNT},   {COP_NE_I, W_I64_NE, H_COUNT},   {COP_LT_I, W_I64_LT_S, H_COUNT},
        {COP_GT_I, W_I64_GT_S, H_COUNT}, {COP_LE_I, W_I64_LE_S, H_COUNT}, {COP_GE_I, W_I64_GE_S, H_COUNT},
        {COP_EQ_F, W_F64_EQ, H_COUNT},   {COP_NE_F, W_F64_NE, H_COUNT},   {COP_LT_F, W_F64_LT, H_COUNT},
        {COP_GT_F, W_F64_GT, H_COUNT},   {COP_LE_F, W_F64_LE, H_COUNT},   {COP_GE_F, W_F64_GE, H_COUNT},
        {COP_EQ_B, W_I32_EQ, H_COUNT},   {COP_NE_B, W_I32_NE, H_COUNT},   {COP_NOT, W_I32_EQZ, H_COUNT},
        {COP_NEG_F, W_F64_NEG, H_COUNT}, {COP_CONCAT, -1, H_CONCAT},       {-1, -1, H_COUNT},
    };
    for (int i = 0; table[i].cop >= 0; ++i) {
        if (table[i].cop == c) {
            if (table[i].op >= 0) {
                op(w, (uint8_t)table[i].op);
            } else {
                host(w, table[i].host);
            }

            return;
        }
    }

    switch (c) {
        case COP_EQ_S:
            host(w, H_STR_EQ);

            return;
        case COP_NE_S:
            host(w, H_STR_EQ);
            op(w, W_I32_EQZ);

            return;
        case COP_LT_S:
        case COP_GT_S:
        case COP_LE_S:
        case COP_GE_S: {
            host(w, H_STR_CMP);
            i32c(w, 0);
            static const uint8_t cmp[] = {0x48, 0x4A, 0x4C, 0x4E}; // i32.lt_s gt_s le_s ge_s
            op(w, cmp[c - COP_LT_S]);

            return;
        }
        case COP_LEN_S:
        case COP_LEN_A:
            mem(w, W_I32_LOAD, 2, 0);
            op(w, W_I64_EXTEND_I32_S);

            return;
        default: wfail(line, w->file, "an operation the wasm target does not know (%d)", c);
    }
}

// start ... end with a step of 1 / -1 (or the given one), as a range object
static void make_range(WCtx* w, Node* recv, NodeList* args, int c, int line) {
    const int r = new_local(w, T_I32);
    const int start = new_local(w, T_I64);
    const int end = new_local(w, T_I64);
    const bool to = c == COP_RANGE_TO || c == COP_RANGE_TO_INCL;
    const bool incl = c == COP_RANGE_INCL || c == COP_RANGE_TO_INCL;
    if (to) {
        op(w, W_I64_CONST);
        sleb(w->b, 0);
        op_u(w, W_LOCAL_SET, start);
        wexpr(w, arg_at(recv, args, 0));
        op_u(w, W_LOCAL_SET, end);
    } else {
        wexpr(w, arg_at(recv, args, 0));
        op_u(w, W_LOCAL_SET, start);
        wexpr(w, arg_at(recv, args, 1));
        op_u(w, W_LOCAL_SET, end);
    }

    alloc(w, 32);
    op_u(w, W_LOCAL_SET, r);
    // the step: 1 counting up, -1 counting down
    const int step = new_local(w, T_I64);
    op_u(w, W_LOCAL_GET, start);
    op_u(w, W_LOCAL_GET, end);
    op(w, W_I64_LE_S);
    open_block(w, W_IF, T_I64);
    op(w, W_I64_CONST);
    sleb(w->b, 1);
    op(w, W_ELSE);
    op(w, W_I64_CONST);
    sleb(w->b, -1);
    close_block(w);
    op_u(w, W_LOCAL_SET, step);
    op_u(w, W_LOCAL_GET, r);
    op_u(w, W_LOCAL_GET, start);
    mem(w, W_I64_STORE, 3, 0);
    op_u(w, W_LOCAL_GET, r);
    op_u(w, W_LOCAL_GET, end);
    if (incl) {
        // a ...= range ends one step past its last element
        op_u(w, W_LOCAL_GET, step);
        op(w, W_I64_ADD);
    }

    mem(w, W_I64_STORE, 3, 8);
    op_u(w, W_LOCAL_GET, r);
    op_u(w, W_LOCAL_GET, step);
    mem(w, W_I64_STORE, 3, 16);
    op_u(w, W_LOCAL_GET, r);
    i32c(w, incl ? 1 : 0);
    mem(w, W_I32_STORE, 2, 24);
    op_u(w, W_LOCAL_GET, r);
    (void)line;
}

// a constructor of an enum variant: [variant][count][payload slots]
static void make_variant(WCtx* w, Node* n, CFunc* f, Node* recv, NodeList* args) {
    const SType* et = wtype(w, n);
    const int argc = args->count + (recv ? 1 : 0);
    const int p = new_local(w, T_I32);
    alloc(w, 8 + 8 * argc);
    op_u(w, W_LOCAL_SET, p);
    op_u(w, W_LOCAL_GET, p);
    i32c(w, f->variant);
    mem(w, W_I32_STORE, 2, 0);
    op_u(w, W_LOCAL_GET, p);
    i32c(w, argc);
    mem(w, W_I32_STORE, 2, 4);
    for (int i = 0; i < argc; ++i) {
        SType* pt = et->kind == ST_ENUM ? variant_payload(G->ch, (SType*)et, f->variant, i) : wtype(w, arg_at(recv, args, i));
        op_u(w, W_LOCAL_GET, p);
        wexpr_to(w, arg_at(recv, args, i), pt);
        store_slot(w, pt->kind == ST_NEVER ? wtype(w, arg_at(recv, args, i)) : pt, 8 + 8 * i, n->line);
    }

    op_u(w, W_LOCAL_GET, p);
}

// the natives the host implements (NATIVES in JS_HOST), by their C name or builtin name
static const char* JS_NATIVES[] = {
    "sort", "map", "filter", "reduce", "join", "contains", "index_of", "reverse", "concat", "slice", "copy", "repeat",
    "multiply", "pop", "insert", "remove", "split", "upper", "lower", "trim", "replace", "starts_with", "ends_with",
    "chars", "ord", "chr", "parse_int", "parse_float", "format", "get_type", "int", "float", "[]", "==", "!=", "in",
    "#", "min", "max", "+", "-", "*", "/", "%", "**", "<", ">", "<=", ">=", "math_pi", "math_e", "math_abs", "math_floor", "math_ceil", "math_round", "math_sqrt", "math_pow",
    "math_trunc", "math_sign", "math_fround", "math_cbrt", "math_exp", "math_expm1", "math_log1p", "math_log",
    "math_log2", "math_log10", "math_sin", "math_cos", "math_tan", "math_asin", "math_acos", "math_atan", "math_atan2",
    "math_sinh", "math_cosh", "math_tanh", "math_asinh", "math_acosh", "math_atanh", "math_hypot2", "math_hypot3",
    "math_hypot_array", "math_clz32", "math_imul", "math_random", "math_random_int", "std_map_new", "std_set_new",
    "std_map_get", "std_map_put", "std_map_has", "std_map_remove", "std_map_keys", "std_map_values", "std_map_entries",
    "std_map_clear", "std_set_add", "std_set_remove", "std_set_union", "std_set_intersection", "std_set_difference",
    "std_in_range", "std_range_has", NULL,
};

static const char* native_key(const CFunc* f) {
    const char* key = f->impl_name ? f->impl_name->chars : f->name->chars;
    for (int i = 0; JS_NATIVES[i]; ++i) {
        if (strcmp(JS_NATIVES[i], key) == 0) {
            return key;
        }
    }

    return NULL;
}

// a call of a native the host runs: the arguments (of the static types ts, already in the
// buffer's slots) go to the host's NATIVES[key], the result comes back in the slot after them
static void bridge_call(WCtx* w, int buf, const char* key, SType** ts, int argc, SType* rt, int line) {
    sb_printf(&G->sigjs, "SIGS[%d]=[", G->nsigs);
    js_string(&G->sigjs, key);
    sb_puts(&G->sigjs, ",[");
    for (int i = 0; i < argc; ++i) {
        sb_printf(&G->sigjs, i ? ",%d" : "%d", type_desc(ts[i]));
    }

    const bool has = rt && valtype(rt, line, w->file);
    sb_printf(&G->sigjs, "],%d];\n", has ? type_desc(rt) : -1);
    i32c(w, G->nsigs++);
    op_u(w, W_LOCAL_GET, (uint64_t)buf);
    host(w, H_NATIVE);
    if (has) {
        op_u(w, W_LOCAL_GET, (uint64_t)buf);
        load_slot(w, rt, 8 * argc, line);
    }
}

static int bridge_buf(WCtx* w, int argc) {
    const int buf = new_local(w, T_I32);
    alloc(w, 8 * argc + 8);
    op_u(w, W_LOCAL_SET, (uint64_t)buf);

    return buf;
}

// the value on the stack (of type t) into argument slot i
static void bridge_store(WCtx* w, int buf, int i, const SType* t, int line) {
    const uint8_t vt = valtype(t, line, w->file);
    const int v = new_local(w, vt);
    op_u(w, W_LOCAL_SET, (uint64_t)v);
    op_u(w, W_LOCAL_GET, (uint64_t)buf);
    op_u(w, W_LOCAL_GET, (uint64_t)v);
    store_slot(w, t, 8 * i, line);
}

static void bridge(WCtx* w, Node* n, const char* key, Node* recv, NodeList* args) {
    const int argc = args->count + (recv ? 1 : 0);
    const int buf = bridge_buf(w, argc);
    SType** ts = pp_calloc((size_t)argc + 1, sizeof(SType*));
    for (int i = 0; i < argc; ++i) {
        Node* a = recv ? (i == 0 ? recv : args->items[i - 1]) : args->items[i];
        ts[i] = wtype(w, a);
        wexpr(w, a);
        bridge_store(w, buf, i, ts[i], n->line);
    }

    bridge_call(w, buf, key, ts, argc, wtype(w, n), n->line);
    free(ts);
}

static bool is_module_fn(CFunc* f, const char* module, const char* name) {
    return f->module && f->module->name && str_equals(f->module->name, module) && str_equals(f->name, name);
}

static bool is_generator_type(const SType* t);
static void gen_advance(WCtx* w, int g);
static void variant_value(WCtx* w, int variant, SType* pt, int line);

static void native_call(WCtx* w, Node* n, CFunc* f, Node* recv, NodeList* args) {
    const int argc = args->count + (recv ? 1 : 0);
    const char* name = f->name->chars;
    if (is_module_fn(f, "io", "print") || is_module_fn(f, "io", "print_nl")) {
        // every argument first (one may print too), then the line
        int* vals = pp_calloc((size_t)argc + 1, sizeof(int));
        for (int i = 0; i < argc; ++i) {
            Node* a = arg_at(recv, args, i);
            wexpr(w, a);
            vals[i] = new_local(w, valtype(wtype(w, a), n->line, w->file));
            op_u(w, W_LOCAL_SET, (uint64_t)vals[i]);
        }

        for (int i = 0; i < argc; ++i) {
            if (i) {
                host(w, H_OUT_SPACE);
            }

            op_u(w, W_LOCAL_GET, (uint64_t)vals[i]);
            to_host(w, wtype(w, arg_at(recv, args, i)), H_OUT_I64, H_OUT_F64, H_OUT_I32, n->line);
        }

        free(vals);

        i32c(w, name[5] == '_' ? 1 : 0);
        host(w, H_OUT_END);

        return;
    }

    if (strcmp(name, "generator_next") == 0 && argc == 1) {
        // the generator's next value: some(value), or none() once it ended
        SType* rt = wtype(w, n);
        int some = -1;
        int none = -1;
        for (int i = 0; rt->kind == ST_ENUM && i < rt->decl->nvariants; ++i) {
            if (str_equals(rt->decl->variants[i].name, "some")) {
                some = i;
            } else if (str_equals(rt->decl->variants[i].name, "none")) {
                none = i;
            }
        }

        Node* a = arg_at(recv, args, 0);
        SType* gt = wtype(w, a);
        if (some < 0 || none < 0 || !is_generator_type(gt) || gt->nargs < 1) {
            unsupported(w, n, "generator_next() of a value that is not a typed Generator");
        }

        SType* pt = variant_payload(G->ch, rt, some, 0);
        const int g = new_local(w, T_I32);
        wexpr(w, a);
        op_u(w, W_LOCAL_SET, (uint64_t)g);
        gen_advance(w, g);
        open_block(w, W_IF, T_I32);
        op_u(w, W_LOCAL_GET, (uint64_t)g);
        load_slot(w, gt->args[0], 8, n->line);
        convert(w, gt->args[0], pt);
        variant_value(w, some, pt, n->line);
        op(w, W_ELSE);
        variant_value(w, none, NULL, n->line);
        close_block(w);

        return;
    }

    const char* key = native_key(f);
    if (!f->builtin) {
        if (key) {
            bridge(w, n, key, recv, args);

            return;
        }

        wfail(n->line, w->file, "the wasm target cannot call native function '%s' yet", name);
    }

    if (strcmp(name, "range") == 0 && argc == 3) {
        // range(start, end, step) of ints; a float anywhere makes a float range, which this target does not have yet
        for (int i = 0; i < 3; ++i) {
            if (wtype(w, arg_at(recv, args, i))->kind != ST_INT) {
                wfail(n->line, w->file, "the wasm target has no float ranges yet");
            }
        }

        const int r = new_local(w, T_I32);
        const int step = new_local(w, T_I64);
        wexpr(w, arg_at(recv, args, 2));
        op_u(w, W_LOCAL_TEE, (uint64_t)step);
        op(w, W_I64_EQZ);
        open_block(w, W_IF, 0);
        i32c(w, str_addr(str_cintern(G->vm, "range step cannot be 0")));
        host(w, H_FAIL);
        close_block(w);
        alloc(w, 32);
        op_u(w, W_LOCAL_TEE, (uint64_t)r);
        wexpr(w, arg_at(recv, args, 0));
        mem(w, W_I64_STORE, 3, 0);
        op_u(w, W_LOCAL_GET, (uint64_t)r);
        wexpr(w, arg_at(recv, args, 1));
        mem(w, W_I64_STORE, 3, 8);
        op_u(w, W_LOCAL_GET, (uint64_t)r);
        op_u(w, W_LOCAL_GET, (uint64_t)step);
        mem(w, W_I64_STORE, 3, 16);
        op_u(w, W_LOCAL_GET, (uint64_t)r);

        return;
    }

    if (strcmp(name, "enum_value") == 0 && argc == 1) {
        // the variants' values are literals of the declaration: a table in the data segment, by variant
        SType* et = wtype(w, arg_at(recv, args, 0));
        SType* rt = wtype(w, n);
        const Node* decl = et->kind == ST_ENUM ? et->decl->node : NULL;
        if (!decl) {
            wfail(n->line, w->file, "enum_value() of a value that is not an enum");
        }

        // the values first (a string literal goes into the data segment too), then the table
        const int nv = decl->as.enum_decl.nvariants;
        uint8_t* values = pp_calloc((size_t)nv + 1, 8);
        for (int i = 0; i < nv; ++i) {
            const Node* v = decl->as.enum_decl.variants[i].value;
            uint8_t slot[8] = {0};
            if (!v || v->kind == N_INT) {
                const int64_t k = v ? v->as.ival : i;
                if (rt->kind == ST_FLOAT) {
                    const double d = (double)k;
                    memcpy(slot, &d, 8);
                } else {
                    memcpy(slot, &k, 8);
                }
            } else if (v->kind == N_FLOAT) {
                memcpy(slot, &v->as.fval, 8);
            } else if (v->kind == N_BOOL) {
                slot[0] = v->as.bval ? 1 : 0;
            } else if (v->kind == N_STRING) {
                const int32_t addr = str_addr(v->as.str);
                memcpy(slot, &addr, 4);
            } else {
                wfail(v->line, w->file, "the wasm target needs the values of an enum to be literals");
            }

            memcpy(values + 8 * i, slot, 8);
        }

        while (G->data.len % 8) {
            put(&G->data, 0);
        }

        const int table = DATA_BASE + (int)G->data.len;
        put_all(&G->data, values, (size_t)nv * 8);
        free(values);

        wexpr(w, arg_at(recv, args, 0));
        mem(w, W_I32_LOAD, 2, 0);
        i32c(w, 3);
        op(w, W_I32_SHL);
        i32c(w, table);
        op(w, W_I32_ADD);
        load_slot(w, rt, 0, n->line);

        return;
    }

    if (strcmp(name, "#") == 0 && argc == 1 && wtype(w, arg_at(recv, args, 0))->kind == ST_TUPLE) {
        op(w, W_I64_CONST);
        sleb(w->b, wtype(w, arg_at(recv, args, 0))->nargs);

        return;
    }

    if ((strcmp(name, "int") == 0 || strcmp(name, "float") == 0 || strcmp(name, "[]") == 0) && argc >= 1 &&
        wtype(w, arg_at(recv, args, 0))->kind == ST_STRING) {
        bridge(w, n, name, recv, args);

        return;
    }

    if (strcmp(name, "str") == 0 && argc == 1) {
        Node* a = arg_at(recv, args, 0);
        wexpr(w, a);
        to_host(w, wtype(w, a), H_STR_I64, H_STR_F64, H_STR_I32, n->line);

        return;
    }

    if (strcmp(name, "push") == 0 && argc == 2) {
        Node* arr = arg_at(recv, args, 0);
        const SType* at = wtype(w, arr);
        wexpr(w, arr);
        host(w, H_ARR_PUSH);
        wexpr_to(w, arg_at(recv, args, 1), at->elem);
        store_slot(w, at->elem, 0, n->line);

        return;
    }

    if (strcmp(name, "int") == 0 && argc == 1) {
        Node* a = arg_at(recv, args, 0);
        wexpr(w, a);
        const STKind k = wtype(w, a)->kind;
        if (k == ST_FLOAT) {
            put(w->b, 0xFC);
            uleb(w->b, 6); // i64.trunc_sat_f64_s
        } else if (k == ST_BOOL) {
            op(w, W_I64_EXTEND_I32_S);
        } else if (k != ST_INT) {
            wfail(n->line, w->file, "the wasm target converts only numbers and bools with int()");
        }

        return;
    }

    if (strcmp(name, "float") == 0 && argc == 1) {
        Node* a = arg_at(recv, args, 0);
        wexpr(w, a);
        const STKind k = wtype(w, a)->kind;
        if (k == ST_INT) {
            op(w, W_F64_CONVERT_I64_S);
        } else if (k != ST_FLOAT) {
            wfail(n->line, w->file, "the wasm target converts only numbers with float()");
        }

        return;
    }

    if (strcmp(name, "panic") == 0 && argc == 1) {
        wexpr(w, arg_at(recv, args, 0));
        if (wtype(w, arg_at(recv, args, 0))->kind != ST_STRING) {
            to_host(w, wtype(w, arg_at(recv, args, 0)), H_STR_I64, H_STR_F64, H_STR_I32, n->line);
        }

        host(w, H_PANIC);
        op(w, W_UNREACHABLE);

        return;
    }

    if (strcmp(name, "type_of") == 0 && argc == 1) {
        i32c(w, str_addr(str_cintern(G->vm, st_name(wtype(w, arg_at(recv, args, 0))))));

        return;
    }

    if (strcmp(name, "tuple") == 0) {
        const int p = new_local(w, T_I32);
        alloc(w, 8 * argc);
        op_u(w, W_LOCAL_SET, p);
        for (int i = 0; i < argc; ++i) {
            op_u(w, W_LOCAL_GET, p);
            wexpr(w, arg_at(recv, args, i));
            store_slot(w, wtype(w, arg_at(recv, args, i)), 8 * i, n->line);
        }

        op_u(w, W_LOCAL_GET, p);

        return;
    }

    if (strcmp(name, "[]=") == 0 && argc == 3 && wtype(w, arg_at(recv, args, 0))->kind == ST_ARRAY) {
        // xs.[]=(i, v), written as a call
        const SType* ot = wtype(w, arg_at(recv, args, 0));
        wexpr(w, arg_at(recv, args, 0));
        wexpr(w, arg_at(recv, args, 1));
        host(w, H_ARR_SLOT);
        wexpr_to(w, arg_at(recv, args, 2), ot->elem);
        store_slot(w, ot->elem, 0, n->line);

        return;
    }

    if (strcmp(name, "[]") == 0 && argc == 2) {
        Node* obj = arg_at(recv, args, 0);
        Node* key = arg_at(recv, args, 1);
        const SType* ot = wtype(w, obj);
        if (ot->kind == ST_ARRAY) {
            wexpr(w, obj);
            wexpr(w, key);
            host(w, H_ARR_SLOT);
            load_slot(w, ot->elem, 0, n->line);

            return;
        }

        if (ot->kind == ST_TUPLE && key->kind == N_INT) {
            const int64_t k = key->as.ival < 0 ? key->as.ival + ot->nargs : key->as.ival;
            wexpr(w, obj);
            load_slot(w, ot->args[k], (int)(8 * k), n->line);

            return;
        }

        if (ot->kind == ST_TUPLE) {
            // a computed key: the value has the elements' joined type (often a union, then boxed)
            SType* rt = wtype(w, n);
            const int tp = new_local(w, T_I32);
            const int kl = new_local(w, T_I64);
            wexpr(w, obj);
            op_u(w, W_LOCAL_SET, (uint64_t)tp);
            wexpr(w, key);
            op_u(w, W_LOCAL_TEE, (uint64_t)kl);
            op(w, W_I64_CONST);
            sleb(w->b, 0);
            op(w, W_I64_LT_S);
            open_block(w, W_IF, 0);
            op_u(w, W_LOCAL_GET, (uint64_t)kl);
            op(w, W_I64_CONST);
            sleb(w->b, ot->nargs);
            op(w, W_I64_ADD);
            op_u(w, W_LOCAL_SET, (uint64_t)kl);
            close_block(w);
            open_block(w, W_BLOCK, valtype(rt, n->line, w->file));
            const int out = w->labels;
            for (int k = 0; k < ot->nargs; ++k) {
                op_u(w, W_LOCAL_GET, (uint64_t)kl);
                op(w, W_I64_CONST);
                sleb(w->b, k);
                op(w, W_I64_EQ);
                open_block(w, W_IF, 0);
                op_u(w, W_LOCAL_GET, (uint64_t)tp);
                load_slot(w, ot->args[k], 8 * k, n->line);
                convert(w, ot->args[k], rt);
                op_u(w, W_BR, (uint64_t)(w->labels - out));
                close_block(w);
            }

            StrBuf sb;
            sb_init(&sb);
            sb_printf(&sb, "tuple index out of range (length %d)", ot->nargs);
            i32c(w, str_addr(str_cintern(G->vm, sb.data)));
            sb_free(&sb);
            host(w, H_FAIL);
            op(w, W_UNREACHABLE);
            close_block(w);

            return;
        }

        if (ot->kind == ST_RECORD && key->kind == N_STRING) {
            wexpr(w, obj);
            i32c(w, name_id(key->as.str));
            host(w, H_FIELD);
            load_slot(w, wtype(w, n), 0, n->line);

            return;
        }

        wfail(n->line, w->file, "the wasm target cannot index a value of type %s yet", st_name(ot));
    }

    if (key) {
        bridge(w, n, key, recv, args);

        return;
    }

    wfail(n->line, w->file, "the wasm target cannot call '%s' yet", name);
}

static void dispatch(WCtx* w, Node* n, const CCall* call, Node* recv, NodeList* args);
static void call_value(WCtx* w, Node* n, Node* callee, Node* recv, NodeList* args);
static void check_exit(WCtx* w);
static void emit_return(WCtx* w, int line);

// lift(x): the value of some(v) / success(v), or a return of x from the function
static void lift(WCtx* w, Node* n, Node* a) {
    SType* t = wtype(w, a);
    const char* bad = t->kind == ST_ENUM && t->decl == G->ch->option ? "none" : "fail";
    int variant = -1;
    for (int i = 0; t->kind == ST_ENUM && i < t->decl->nvariants; ++i) {
        if (str_equals(t->decl->variants[i].name, bad)) {
            variant = i;
        }
    }

    if (variant < 0) {
        wfail(n->line, w->file, "lift() of a value that is not an Option or an Error");
    }

    const int v = new_local(w, T_I32);
    wexpr(w, a);
    op_u(w, W_LOCAL_TEE, v);
    mem(w, W_I32_LOAD, 2, 0);
    i32c(w, variant);
    op(w, W_I32_EQ);
    open_block(w, W_IF, 0);
    const SType* ret = w->inst->ret;
    if (ret && ret->kind == ST_ENUM && ret->decl == t->decl) {
        op_u(w, W_LOCAL_GET, v);
        emit_return(w, n->line);
    } else {
        StrBuf sb;
        sb_init(&sb);
        sb_printf(&sb, "lift() met %s() in a function that returns %s", bad, st_name(ret ? ret : st_prim(ST_VOID)));
        i32c(w, str_addr(str_cintern(G->vm, sb.data)));
        sb_free(&sb);
        host(w, H_FAIL);
    }

    close_block(w);
    op_u(w, W_LOCAL_GET, v);
    load_slot(w, wtype(w, n), 8, n->line);
}

// does a call pass a scope's block on: then it may come back with the block's early exit
static bool passes_block(WCtx* w, Node* recv, NodeList* args) {
    if (recv && wtype(w, recv)->kind == ST_BLOCK) {
        return true;
    }

    for (int i = 0; i < args->count; ++i) {
        if (wtype(w, args->items[i])->kind == ST_BLOCK) {
            return true;
        }
    }

    return false;
}

static void wcall(WCtx* w, Node* n, const CCall* call, Node* recv, NodeList* args, Node* callee) {
    const int argc = args->count + (recv ? 1 : 0);
    if (call->dispatch || (call->func && call->func->kind == CF_REQ)) {
        dispatch(w, n, call, recv, args);

        return;
    }

    if (call->func && call->func->kind == CF_NATIVE && call->func->builtin && str_equals(call->func->name, "lift") && argc == 1) {
        lift(w, n, arg_at(recv, args, 0));

        return;
    }

    if (call->op == COP_RANGE || call->op == COP_RANGE_TO || call->op == COP_RANGE_INCL || call->op == COP_RANGE_TO_INCL) {
        make_range(w, recv, args, call->op, n->line);

        return;
    }

    if (call->op == COP_NEG_I) {
        op(w, W_I64_CONST);
        sleb(w->b, 0);
        wexpr(w, arg_at(recv, args, 0));
        op(w, W_I64_SUB);

        return;
    }

    if (call->op) {
        const bool f = float_cop(call->op);
        for (int i = 0; i < argc; ++i) {
            Node* a = arg_at(recv, args, i);
            wexpr(w, a);
            if (f && wtype(w, a)->kind == ST_INT) {
                op(w, W_F64_CONVERT_I64_S);
            }
        }

        cop(w, call->op, n->line);

        return;
    }

    if (call->target) {
        CInstance* t = call->target;
        if (t->fn && is_nested_fn(t->fn)) {
            // a local function: through its closure, kept in a cell named after the instance
            char key[32];
            snprintf(key, sizeof key, "\x01fn%d", t->id);
            const int c = new_local(w, T_I32);
            if (!load_var(w, str_cintern(G->vm, key))) {
                wfail(n->line, w->file, "the wasm target cannot reach the local function '%s' from here", t->fn->name->chars);
            }

            op_u(w, W_LOCAL_TEE, c);
            int i = 0;
            if (recv) {
                wexpr_to(w, recv, t->nparams ? t->params[0] : NULL);
                i++;
            }

            for (int k = 0; k < args->count; ++k, ++i) {
                wexpr_to(w, args->items[k], i < t->nparams ? t->params[i] : NULL);
            }

            op_u(w, W_LOCAL_GET, c);
            mem(w, W_I32_LOAD, 2, 0);
            op_u(w, W_CALL_INDIRECT, (uint64_t)wfunc_for(t)->type);
            put(w->b, 0);
        } else {
            push_args(w, recv, args, t);
            op_u(w, W_CALL, wfunc_for(t)->index);
        }

        if (passes_block(w, recv, args)) {
            check_exit(w);
        }

        return;
    }

    CFunc* f = call->func;
    if (f && f->kind == CF_CTOR) {
        make_variant(w, n, f, recv, args);

        return;
    }

    if (f && f->kind == CF_NATIVE) {
        native_call(w, n, f, recv, args);

        return;
    }

    if (!callee) {
        unsupported(w, n, "this call");
    }

    call_value(w, n, callee, recv, args);
}

// a call of a value of a function type (a lambda, a function, a scope's block) through the table
static void call_value(WCtx* w, Node* n, Node* callee, Node* recv, NodeList* args) {
    SType* ct = wtype(w, callee);
    if (ct->kind == ST_TYPE && args->count == 1 && !recv) {
        // Person({...}): the checker checked the value; a literal takes the record's field types
        wexpr_to(w, args->items[0], wtype(w, n));

        return;
    }

    if (ct->kind != ST_FUNC && ct->kind != ST_BLOCK) {
        unsupported(w, n, "calling a value of this type");
    }

    if (ct->kind == ST_FUNC && ct->variadic) {
        unsupported(w, n, "calling a function value with variadic parameters");
    }

    if (ct->kind == ST_BLOCK && ct->variadic && valtype(wtype(w, n), n->line, w->file)) {
        unsupported(w, n, "using the value of a Callback");
    }

    const int c = new_local(w, T_I32);
    wexpr(w, callee);
    op_u(w, W_LOCAL_TEE, c);
    int i = 0;
    if (recv) {
        wexpr_to(w, recv, ct->kind == ST_FUNC && ct->nargs ? ct->args[0] : ct->elem);
        i++;
    }

    for (int k = 0; k < args->count; ++k, ++i) {
        const SType* pt = ct->kind == ST_BLOCK ? ct->elem : i < ct->nargs ? ct->args[i] : NULL;
        wexpr_to(w, args->items[k], pt);
    }

    op_u(w, W_LOCAL_GET, c);
    mem(w, W_I32_LOAD, 2, 0);
    op_u(w, W_CALL_INDIRECT, (uint64_t)call_type(ct, n->line, w->file));
    put(w->b, 0);
    if (ct->kind == ST_BLOCK || passes_block(w, recv, args)) {
        check_exit(w);
    }
}

// The candidates of a call that needs dynamic dispatch (an argument is a box: unknown, a union
// or an interface), the most specific first: the first whose parameters the boxed values fit
// is called, with the values unboxed to its parameter types.
static void dispatch_values(WCtx* w, Node* n, const CCall* call, SType** sargs, int* locals, int argc, SType* rt);

static void dispatch(WCtx* w, Node* n, const CCall* call, Node* recv, NodeList* args) {
    const int argc = args->count + (recv ? 1 : 0);
    SType** sargs = pp_calloc((size_t)argc + 1, sizeof(SType*));
    int* locals = pp_calloc((size_t)argc + 1, sizeof(int));
    for (int i = 0; i < argc; ++i) {
        Node* a = arg_at(recv, args, i);
        sargs[i] = wtype(w, a);
        const uint8_t vt = valtype(sargs[i], n->line, w->file);
        locals[i] = new_local(w, vt ? vt : T_I32);
        wexpr(w, a);
        op_u(w, W_LOCAL_SET, (uint64_t)locals[i]);
    }

    dispatch_values(w, n, call, sargs, locals, argc, wtype(w, n));
    free(sargs);
    free(locals);
}

// the dispatch itself: the arguments (of the static types sargs) are in the locals; rt: the call's type
static void dispatch_values(WCtx* w, Node* n, const CCall* call, SType** sargs, int* locals, int argc, SType* rt) {
    Checker* ch = G->ch;
    CFunc* single[1] = {call->func};
    CFunc** given = call->cands ? call->cands : single;
    const int ngiven = call->cands ? call->ncands : 1;
    CFunc** cands = pp_calloc((size_t)(ngiven + ch->nimpl_fns) + 1, sizeof(CFunc*));
    int ncands = 0;
    for (int g = 0; g < ngiven; ++g) {
        CFunc* f = given[g];
        for (int i = 0; f && f->kind == CF_REQ && i < ch->nimpl_fns; ++i) {
            bool seen = !str_equals(ch->impl_fns[i].name, f->name->chars);
            for (int k = 0; k < ncands; ++k) {
                seen = seen || cands[k] == ch->impl_fns[i].fn;
            }

            if (!seen) {
                cands[ncands++] = ch->impl_fns[i].fn;
            }
        }

        bool seen = !f || f->kind == CF_REQ;
        for (int k = 0; k < ncands; ++k) {
            seen = seen || cands[k] == f;
        }

        if (!seen) {
            cands[ncands++] = f;
        }
    }

    const uint8_t rvt = valtype(rt, n->line, w->file);
    open_block(w, W_BLOCK, rvt);
    const int out = w->labels;
    int tried = 0;
    for (int c = 0; c < ncands; ++c) {
        CFunc* f = cands[c];
        SType** params = NULL;
        int nparams = 0;
        const char* host_key = f->kind == CF_NATIVE && !f->builtin ? native_key(f) : NULL;
        if (f->kind != CF_USER && !host_key) {
            wfail(n->line, w->file, "the wasm target cannot dispatch the call of '%s' to a native function", f->name->chars);
        }

        CInstance* inst = checker_declared_instance(ch, f, sargs, argc, &params, &nparams);
        // a value of an interface type is one of the types that implement it with its arguments:
        // iter(string) is no candidate for an Iterable<int>
        bool possible = nparams == argc;
        for (int i = 0; possible && i < argc; ++i) {
            if (sargs[i]->kind == ST_IFACE && params[i] && !is_boxed(params[i]) && !st_has_param(params[i]) &&
                st_fit(sargs[i], params[i], NULL) <= 0) {
                possible = false;
            }

            if (params[i] && !representable(params[i])) {
                possible = false; // no value of that type exists in this target (yet), so no box holds one
            }
        }

        if (!possible) {
            continue;
        }

        if (host_key) {
            // a native the host runs (get of a Map through Index): the values, unboxed to its
            // parameter types, go to the host, the result comes back as the call's type
            bool concrete = nparams == argc && !(f->sig && f->sig->variadic);
            for (int i = 0; concrete && i < argc; ++i) {
                concrete = params[i] && !st_has_param(params[i]) && representable(params[i]);
            }

            if (!concrete) {
                continue;
            }

            tried++;
            open_block(w, W_BLOCK, 0);
            const int next = w->labels;
            for (int i = 0; i < argc; ++i) {
                const bool other_iface = params[i]->kind == ST_IFACE && !st_equal(params[i], sargs[i]);
                if (is_boxed(sargs[i]) && (!is_boxed(params[i]) || other_iface)) {
                    op_u(w, W_LOCAL_GET, locals[i]);
                    mem(w, W_I32_LOAD, 2, 0);
                    op_u(w, W_CALL, (uint64_t)type_test(params[i]));
                    op(w, W_I32_EQZ);
                    op_u(w, W_BR_IF, (uint64_t)(w->labels - next));
                }
            }

            const int buf = bridge_buf(w, argc);
            for (int i = 0; i < argc; ++i) {
                op_u(w, W_LOCAL_GET, locals[i]);
                if (is_boxed(sargs[i]) && !is_boxed(params[i])) {
                    unbox(w, params[i], n->line);
                } else {
                    convert(w, sargs[i], params[i]);
                }

                bridge_store(w, buf, i, params[i], n->line);
            }

            bridge_call(w, buf, host_key, params, argc, rvt ? rt : NULL, n->line);
            op_u(w, W_BR, (uint64_t)(w->labels - out));
            close_block(w);

            continue;
        }

        if (!inst || is_nested_fn(f) || nparams != argc || (f->sig && f->sig->variadic)) {
            wfail(n->line, w->file, "the wasm target cannot dispatch the call of '%s' to %s", f->name->chars,
                  f->kind == CF_NATIVE ? "a native function" : "a generic or local function");
        }

        tried++;
        open_block(w, W_BLOCK, 0);
        const int next = w->labels;
        for (int i = 0; i < argc; ++i) {
            const bool other_iface = params[i]->kind == ST_IFACE && !st_equal(params[i], sargs[i]);
            if (is_boxed(sargs[i]) && (!is_boxed(params[i]) || other_iface)) {
                op_u(w, W_LOCAL_GET, locals[i]);
                mem(w, W_I32_LOAD, 2, 0);
                op_u(w, W_CALL, (uint64_t)type_test(params[i]));
                op(w, W_I32_EQZ);
                op_u(w, W_BR_IF, (uint64_t)(w->labels - next));
            }
        }

        i32c(w, 0);
        for (int i = 0; i < argc; ++i) {
            op_u(w, W_LOCAL_GET, locals[i]);
            if (is_boxed(sargs[i]) && !is_boxed(params[i])) {
                unbox(w, params[i], n->line);
            } else {
                convert(w, sargs[i], params[i]);
            }
        }

        op_u(w, W_CALL, wfunc_for(inst)->index);
        if (rvt) {
            convert(w, inst->ret, rt);
        } else if (valtype(inst->ret, n->line, w->file)) {
            op(w, W_DROP);
        }

        op_u(w, W_BR, (uint64_t)(w->labels - out));
        close_block(w);
    }

    if (!tried) {
        wfail(n->line, w->file, "no implementation of '%s' is known to dispatch to", call->func ? call->func->name->chars : "?");
    }

    StrBuf sb;
    sb_init(&sb);
    sb_printf(&sb, "no function '%s' takes these values (line %d)", call->func ? call->func->name->chars : "?", n->line);
    i32c(w, str_addr(str_cintern(G->vm, sb.data)));
    sb_free(&sb);
    host(w, H_FAIL);
    op(w, W_UNREACHABLE);
    close_block(w);
    free(cands);
}



// ---------------------------------------------------------------- function values

static void compile_body(WFunc* wf, CModule* m, Node* program, WCtx* outer);
static void block_body(WCtx* w, Node* block, bool value, const SType* want);

// a function value of f with captures from w (a closure); rec: the local with the scope call
// record of a block, or -1
static void closure_from(WCtx* w, WFunc* f, int rec) {
    if (f->ncaps == 0 && rec < 0) {
        // nothing captured: one value in the data segment serves every use
        while (G->data.len % 8) {
            put(&G->data, 0);
        }

        const int addr = DATA_BASE + (int)G->data.len;
        const uint32_t words[2] = {(uint32_t)table_slot(f), 0};
        put_all(&G->data, words, 8);
        i32c(w, addr);

        return;
    }

    const int p = new_local(w, T_I32);
    alloc(w, 8 + 8 * f->ncaps);
    op_u(w, W_LOCAL_TEE, p);
    i32c(w, table_slot(f));
    mem(w, W_I32_STORE, 2, 0);
    if (rec >= 0) {
        op_u(w, W_LOCAL_GET, p);
        op_u(w, W_LOCAL_GET, (uint64_t)rec);
        mem(w, W_I32_STORE, 2, 4);
    }

    for (int k = 0; k < f->ncaps; ++k) {
        op_u(w, W_LOCAL_GET, p);
        if (f->caps[k].from_local) {
            op_u(w, W_LOCAL_GET, (uint64_t)f->caps[k].index);
        } else {
            cap_cell(w, f->caps[k].index);
        }

        mem(w, W_I32_STORE, 2, 8 + 8 * k);
    }

    op_u(w, W_LOCAL_GET, p);
}

// the closure of a nested function (a lambda, a local function), compiled here so its captures
// come from w
static void make_closure(WCtx* w, CInstance* inst, int line) {
    if (!inst) {
        wfail(line, w->file, "a function without a typed instance");
    }

    WFunc* f = wfunc_for(inst);
    if (!f->done) {
        compile_body(f, inst->fn && inst->fn->module ? inst->fn->module : w->module, NULL, w);
    }

    closure_from(w, f, -1);
}

// a function's name used as a value
static void function_value(WCtx* w, Node* n, SType* t) {
    CFuncSet* set = t->funcs;
    CInstance* only = NULL;
    int users = 0;
    for (int i = 0; i < set->count; ++i) {
        CFunc* f = set->items[i];
        if (f->kind == CF_USER) {
            users++;
            if (f->ninsts == 1) {
                only = f->insts[0];
            }
        }
    }

    if (users != 1 || !only) {
        unsupported(w, n, "a function value of a native function, or of a name with several functions");
    }

    if (is_nested_fn(only->fn)) {
        char key[32];
        snprintf(key, sizeof key, "\x01fn%d", only->id);
        if (!load_var(w, str_cintern(G->vm, key))) {
            wfail(n->line, w->file, "the wasm target cannot reach the local function '%s' from here", only->fn->name->chars);
        }

        return;
    }

    closure_from(w, wfunc_for(only), -1);
}

// the end of a function's code: the locals go in front of it
static void finish_code(WCtx* w, WFunc* wf) {
    op(w, W_END);
    Bytes code = {0};
    uleb(&code, (uint64_t)w->nlocal_types);
    for (int i = 0; i < w->nlocal_types; ++i) {
        uleb(&code, 1);
        put(&code, w->local_types[i]);
    }

    put_all(&code, wf->body.data, wf->body.len);
    free(wf->body.data);
    wf->body = code;
    wf->done = true;
    wf->ncaps = w->ncaps;
    wf->caps = pp_calloc((size_t)w->ncaps + 1, sizeof(WCapRef));
    for (int i = 0; i < w->ncaps; ++i) {
        wf->caps[i] = w->caps[i].ref;
    }

    free(w->locals);
    free(w->local_types);
    free(w->caps);
    free(w->boxnames.items);
}

// The body of a scope call's block (or of a defer) as a closure made in w: it was typed as part of
// w's instance and captures w's variables like a lambda. A block takes `scope` and gives its last
// expression. rec: the local with the scope call's record, which the closure keeps.
static void inline_closure(WCtx* w, Node* body, WKind kind, SType* bt, ObjString* scope, int rec, int line) {
    SType* param = kind == WK_DEFER || !bt || !bt->elem || !valtype(bt->elem, line, w->file) ? NULL : bt->elem;
    SType* value = kind == WK_BLOCK && bt && bt->nargs && valtype(bt->args[0], line, w->file) ? bt->args[0] : NULL;
    char params[3] = {'i', 0, 0};
    char result[2] = {0};
    if (param) {
        params[1] = letter(valtype(param, line, w->file));
    }

    if (value) {
        result[0] = letter(valtype(value, line, w->file));
    }

    WFunc* f = wfunc_add(func_type(params, result));
    f->nested = true;
    WCtx b = {0};
    b.inst = w->inst;
    b.wf = f;
    b.module = w->module;
    b.b = &f->body;
    b.file = w->file;
    b.outer = w;
    b.kind = kind;
    b.scope = scope;
    b.result_vt = value ? valtype(value, line, w->file) : 0;
    b.nparams = param ? 2 : 1;
    names_used_nested(&b.boxnames, body, false);
    if (param) {
        if (names_has(&b.boxnames, G->ch->s_scope)) {
            op_u(&b, W_LOCAL_GET, 1);
            begin(&b);
            declare(&b, G->ch->s_scope, param, line);
        } else {
            GROW(b.locals, b.nlocals, b.caplocals);
            WLocal* l = &b.locals[b.nlocals++];
            memset(l, 0, sizeof *l);
            l->name = G->ch->s_scope;
            l->index = 1;
            l->vt = valtype(param, line, w->file);
            l->type = param;
        }
    }

    block_body(&b, body, value != NULL, value);
    finish_code(&b, f);
    closure_from(w, f, rec);
}

// return with nothing to say: the default value of the function's result (a scope call that
// ended early ignores it)
static void return_default(WCtx* w) {
    switch (w->result_vt) {
        case T_I64:
            op(w, W_I64_CONST);
            sleb(w->b, 0);
            break;
        case T_F64: {
            const double z = 0;
            op(w, W_F64_CONST);
            put_all(w->b, &z, 8);
            break;
        }
        case T_I32: i32c(w, 0); break;
        default: break;
    }

    op(w, W_RETURN);
}

// after a call that may come back with a block's early exit: leave too, toward its scope call
static void check_exit(WCtx* w) {
    op_u(w, W_GLOBAL_GET, (uint64_t)G->g_exit);
    open_block(w, W_IF, 0);
    return_default(w);
    close_block(w);
}

static int ret_global(uint8_t vt) {
    return G->g_ret[vt == T_I64 ? 1 : vt == T_F64 ? 2 : 0];
}

// a block leaves its scope call: exit is a ScopeExit; a Callback cannot
static void leave_block(WCtx* w, int exit, int line) {
    if (w->kind != WK_BLOCK) {
        const char* what = exit == SCOPE_BREAK ? "break" : exit == SCOPE_CONTINUE ? "continue" : "return";
        StrBuf sb;
        sb_init(&sb);
        if (w->kind == WK_CALLBACK) {
            sb_printf(&sb, "'%s' cannot leave the Callback of '%s': it may run after the scope call ended", what,
                      w->scope ? w->scope->chars : "?");
        } else {
            sb_printf(&sb, "'%s' cannot leave a defer block", what);
        }

        i32c(w, str_addr(str_cintern(G->vm, sb.data)));
        sb_free(&sb);
        host(w, H_FAIL);
        op(w, W_UNREACHABLE);

        return;
    }

    i32c(w, exit);
    op_u(w, W_GLOBAL_SET, (uint64_t)G->g_exit);
    op_u(w, W_LOCAL_GET, 0);
    mem(w, W_I32_LOAD, 2, 4);
    op_u(w, W_GLOBAL_SET, (uint64_t)G->g_exit_rec);
    return_default(w);
    (void)line;
}

// return, with the value (if the function gives one) on the stack
static void emit_return(WCtx* w, int line) {
    if (w->kind == WK_FUNCTION) {
        op(w, W_RETURN);

        return;
    }

    const uint8_t vt = w->inst->fn ? valtype(w->inst->ret, line, w->file) : 0;
    if (w->kind == WK_BLOCK && vt) {
        op_u(w, W_GLOBAL_SET, (uint64_t)ret_global(vt));
    }

    leave_block(w, SCOPE_RETURN, line);
}

static void emit_leave_loop(WCtx* w, bool cont, int line) {
    if (w->nloops) {
        const WLoop* l = &w->loops[w->nloops - 1];
        op_u(w, W_BR, (uint64_t)(w->labels - (cont ? l->cont_depth : l->exit_depth)));

        return;
    }

    if (w->kind == WK_FUNCTION) {
        op(w, W_UNREACHABLE); // the checker allows break / continue only in loops

        return;
    }

    leave_block(w, cont ? SCOPE_CONTINUE : SCOPE_BREAK, line);
}

// _name args { body }: the scope function is called with the block (a closure of the body) and
// the args; a `const f = x` arg is also a variable of the body. A record [defer][previous] marks
// the running call. When the scope function ended, the defer block runs; then the call ends the
// way the block left: normally (value: the scope function's result), or with an early exit, which
// happens here (or, for another scope call's block, leaves toward it).
static void scope_call(WCtx* w, Node* n, bool value) {
    const CCall* call = &winfo(w, n)->call;
    CInstance* t = call->target;
    if (!t || call->dispatch || t->nparams < 1) {
        unsupported(w, n, "a scope call the checker did not resolve to one scope");
    }

    SType* bt = t->params[0];
    const bool callback = bt->kind == ST_BLOCK && bt->variadic;
    begin(w);
    const int rec = new_local(w, T_I32);
    alloc(w, 8);
    op_u(w, W_LOCAL_TEE, (uint64_t)rec);
    op_u(w, W_GLOBAL_GET, (uint64_t)G->g_scope);
    mem(w, W_I32_STORE, 2, 4);
    NodeList* al = &n->as.scall.args;
    int* locs = pp_calloc((size_t)al->count + 1, sizeof(int));
    for (int i = 0; i < al->count; ++i) {
        Node* a = al->items[i];
        SType* pt = i + 1 < t->nparams ? t->params[i + 1] : NULL;
        if (a->kind == S_LET) {
            wexpr(w, a->as.let.init);
            declare(w, a->as.let.name, wtype(w, a->as.let.init), a->line);
            locs[i] = -1;
        } else {
            wexpr_to(w, a, pt);
            locs[i] = new_local(w, valtype(pt ? pt : wtype(w, a), n->line, w->file));
            op_u(w, W_LOCAL_SET, (uint64_t)locs[i]);
        }
    }

    const int blk = new_local(w, T_I32);
    inline_closure(w, n->as.scall.body, callback ? WK_CALLBACK : WK_BLOCK, bt, n->as.scall.name, rec, n->line);
    op_u(w, W_LOCAL_SET, (uint64_t)blk);
    op_u(w, W_LOCAL_GET, (uint64_t)rec);
    op_u(w, W_GLOBAL_SET, (uint64_t)G->g_scope);
    const bool nested = t->fn && is_nested_fn(t->fn);
    const int fnv = nested ? new_local(w, T_I32) : -1;
    if (nested) {
        char key[32];
        snprintf(key, sizeof key, "\x01fn%d", t->id);
        if (!load_var(w, str_cintern(G->vm, key))) {
            wfail(n->line, w->file, "the wasm target cannot reach the local scope '%s' from here", t->fn->name->chars);
        }

        op_u(w, W_LOCAL_TEE, (uint64_t)fnv);
    } else {
        i32c(w, 0);
    }

    op_u(w, W_LOCAL_GET, (uint64_t)blk);
    for (int i = 0; i < al->count; ++i) {
        if (locs[i] < 0) {
            load_var(w, al->items[i]->as.let.name);
            convert(w, wtype(w, al->items[i]->as.let.init), i + 1 < t->nparams ? t->params[i + 1] : NULL);
        } else {
            op_u(w, W_LOCAL_GET, (uint64_t)locs[i]);
        }
    }

    free(locs);
    WFunc* sf = wfunc_for(t);
    if (nested) {
        op_u(w, W_LOCAL_GET, (uint64_t)fnv);
        mem(w, W_I32_LOAD, 2, 0);
        op_u(w, W_CALL_INDIRECT, (uint64_t)sf->type);
        put(w->b, 0);
    } else {
        op_u(w, W_CALL, (uint64_t)sf->index);
    }

    const uint8_t rvt = valtype(t->ret, n->line, w->file);
    const int res = rvt ? new_local(w, rvt) : -1;
    if (rvt) {
        op_u(w, W_LOCAL_SET, (uint64_t)res);
    }

    // ended: the previous call is the innermost again; the cleanup runs with the exit put aside
    op_u(w, W_LOCAL_GET, (uint64_t)rec);
    mem(w, W_I32_LOAD, 2, 4);
    op_u(w, W_GLOBAL_SET, (uint64_t)G->g_scope);
    const int ex = new_local(w, T_I32);
    const int exrec = new_local(w, T_I32);
    op_u(w, W_GLOBAL_GET, (uint64_t)G->g_exit);
    op_u(w, W_LOCAL_SET, (uint64_t)ex);
    op_u(w, W_GLOBAL_GET, (uint64_t)G->g_exit_rec);
    op_u(w, W_LOCAL_SET, (uint64_t)exrec);
    i32c(w, 0);
    op_u(w, W_GLOBAL_SET, (uint64_t)G->g_exit);
    op_u(w, W_LOCAL_GET, (uint64_t)rec);
    mem(w, W_I32_LOAD, 2, 0);
    open_block(w, W_IF, 0);
    op_u(w, W_LOCAL_GET, (uint64_t)rec);
    mem(w, W_I32_LOAD, 2, 0);
    op_u(w, W_LOCAL_GET, (uint64_t)rec);
    mem(w, W_I32_LOAD, 2, 0);
    mem(w, W_I32_LOAD, 2, 0);
    op_u(w, W_CALL_INDIRECT, (uint64_t)func_type("i", ""));
    put(w->b, 0);
    close_block(w);
    finish(w);
    // how the block left
    op_u(w, W_LOCAL_GET, (uint64_t)ex);
    open_block(w, W_IF, 0);
    op_u(w, W_LOCAL_GET, (uint64_t)exrec);
    op_u(w, W_LOCAL_GET, (uint64_t)rec);
    op(w, W_I32_NE);
    open_block(w, W_IF, 0);
    // another scope call's block: keep leaving toward it
    op_u(w, W_LOCAL_GET, (uint64_t)ex);
    op_u(w, W_GLOBAL_SET, (uint64_t)G->g_exit);
    op_u(w, W_LOCAL_GET, (uint64_t)exrec);
    op_u(w, W_GLOBAL_SET, (uint64_t)G->g_exit_rec);
    return_default(w);
    close_block(w);
    op_u(w, W_LOCAL_GET, (uint64_t)ex);
    i32c(w, SCOPE_RETURN);
    op(w, W_I32_EQ);
    open_block(w, W_IF, 0);
    const uint8_t fvt = w->inst->fn ? valtype(w->inst->ret, n->line, w->file) : 0;
    if (fvt) {
        op_u(w, W_GLOBAL_GET, (uint64_t)ret_global(fvt));
    }

    emit_return(w, n->line);
    close_block(w);
    op_u(w, W_LOCAL_GET, (uint64_t)ex);
    i32c(w, SCOPE_BREAK);
    op(w, W_I32_EQ);
    open_block(w, W_IF, 0);
    emit_leave_loop(w, false, n->line);
    close_block(w);
    op_u(w, W_LOCAL_GET, (uint64_t)ex);
    i32c(w, SCOPE_CONTINUE);
    op(w, W_I32_EQ);
    open_block(w, W_IF, 0);
    emit_leave_loop(w, true, n->line);
    close_block(w);
    close_block(w);
    if (value && rvt) {
        op_u(w, W_LOCAL_GET, (uint64_t)res);
    }
}

// an enum value of type et: the variant with the payload on the stack (of type pt), or none (pt NULL)
static void variant_value(WCtx* w, int variant, SType* pt, int line) {
    const int v = pt ? new_local(w, valtype(pt, line, w->file)) : -1;
    if (pt) {
        op_u(w, W_LOCAL_SET, (uint64_t)v);
    }

    const int p = new_local(w, T_I32);
    alloc(w, pt ? 16 : 8);
    op_u(w, W_LOCAL_TEE, (uint64_t)p);
    i32c(w, variant);
    mem(w, W_I32_STORE, 2, 0);
    op_u(w, W_LOCAL_GET, (uint64_t)p);
    i32c(w, pt ? 1 : 0);
    mem(w, W_I32_STORE, 2, 4);
    if (pt) {
        op_u(w, W_LOCAL_GET, (uint64_t)p);
        op_u(w, W_LOCAL_GET, (uint64_t)v);
        store_slot(w, pt, 8, line);
    }

    op_u(w, W_LOCAL_GET, (uint64_t)p);
}

// x as T: some(x) when the value fits T, else none()
static void as_value(WCtx* w, Node* n) {
    SType* from = wtype(w, n->as.as_.expr);
    SType* rt = wtype(w, n);
    int some = -1;
    int none = -1;
    for (int i = 0; rt->kind == ST_ENUM && i < rt->decl->nvariants; ++i) {
        if (str_equals(rt->decl->variants[i].name, "some")) {
            some = i;
        } else if (str_equals(rt->decl->variants[i].name, "none")) {
            none = i;
        }
    }

    if (some < 0 || none < 0) {
        wfail(n->line, w->file, "`as` gives an Option");
    }

    SType* to = variant_payload(G->ch, rt, some, 0);
    wexpr(w, n->as.as_.expr);
    if (!is_boxed(from)) {
        // known when compiled
        if (st_fit(to, from, NULL) > 0) {
            convert(w, from, to);
            variant_value(w, some, to, n->line);
        } else {
            if (valtype(from, n->line, w->file)) {
                op(w, W_DROP);
            }

            variant_value(w, none, NULL, n->line);
        }

        return;
    }

    const int b = new_local(w, T_I32);
    op_u(w, W_LOCAL_TEE, (uint64_t)b);
    mem(w, W_I32_LOAD, 2, 0);
    op_u(w, W_CALL, (uint64_t)type_test(to));
    open_block(w, W_IF, T_I32);
    op_u(w, W_LOCAL_GET, (uint64_t)b);
    unbox(w, to, n->line);
    variant_value(w, some, to, n->line);
    op(w, W_ELSE);
    variant_value(w, none, NULL, n->line);
    close_block(w);
}



// ---------------------------------------------------------------- expressions

static bool chain_bridge(WCtx* w, Node* n, const ChainInfo* ci, int k, const SType* lt, const SType* rt);

static void chain_at(WCtx* w, Node* n, const ChainInfo* ci, int enc) {
    if (enc < 0) {
        wexpr(w, n->as.chain.operands.items[-enc - 1]);

        return;
    }

    const int k = enc;
    const char* o = n->as.chain.ops[k]->chars;
    if (strcmp(o, "&&") == 0 || strcmp(o, "||") == 0) {
        chain_at(w, n, ci, ci->left[k]);
        open_block(w, W_IF, T_I32);
        if (o[0] == '&') {
            chain_at(w, n, ci, ci->right[k]);
            op(w, W_ELSE);
            i32c(w, 0);
        } else {
            i32c(w, 1);
            op(w, W_ELSE);
            chain_at(w, n, ci, ci->right[k]);
        }

        close_block(w);

        return;
    }

    const CCall* call = &ci->calls[k];
    const SType* lt = ci->left[k] < 0 ? wtype(w, n->as.chain.operands.items[-ci->left[k] - 1]) : ci->calls[ci->left[k]].type;
    const SType* rt = ci->right[k] < 0 ? wtype(w, n->as.chain.operands.items[-ci->right[k] - 1]) : ci->calls[ci->right[k]].type;

    if (call->op == COP_RANGE || call->op == COP_RANGE_INCL) {
        // the operands of a chain are not nodes: evaluate them into the range directly
        const int start = new_local(w, T_I64);
        const int end = new_local(w, T_I64);
        chain_at(w, n, ci, ci->left[k]);
        op_u(w, W_LOCAL_SET, start);
        chain_at(w, n, ci, ci->right[k]);
        op_u(w, W_LOCAL_SET, end);
        Node a = {0}, b = {0};
        (void)a;
        (void)b;
        // build it like make_range, from the locals
        const int r = new_local(w, T_I32);
        const int step = new_local(w, T_I64);
        alloc(w, 32);
        op_u(w, W_LOCAL_SET, r);
        op_u(w, W_LOCAL_GET, start);
        op_u(w, W_LOCAL_GET, end);
        op(w, W_I64_LE_S);
        open_block(w, W_IF, T_I64);
        op(w, W_I64_CONST);
        sleb(w->b, 1);
        op(w, W_ELSE);
        op(w, W_I64_CONST);
        sleb(w->b, -1);
        close_block(w);
        op_u(w, W_LOCAL_SET, step);
        op_u(w, W_LOCAL_GET, r);
        op_u(w, W_LOCAL_GET, start);
        mem(w, W_I64_STORE, 3, 0);
        op_u(w, W_LOCAL_GET, r);
        op_u(w, W_LOCAL_GET, end);
        if (call->op == COP_RANGE_INCL) {
            op_u(w, W_LOCAL_GET, step);
            op(w, W_I64_ADD);
        }

        mem(w, W_I64_STORE, 3, 8);
        op_u(w, W_LOCAL_GET, r);
        op_u(w, W_LOCAL_GET, step);
        mem(w, W_I64_STORE, 3, 16);
        op_u(w, W_LOCAL_GET, r);
        i32c(w, call->op == COP_RANGE_INCL ? 1 : 0);
        mem(w, W_I32_STORE, 2, 24);
        op_u(w, W_LOCAL_GET, r);

        return;
    }

    if (call->dispatch || (call->func && call->func->kind == CF_REQ)) {
        SType* ts[2] = {(SType*)lt, (SType*)rt};
        int locals[2];
        chain_at(w, n, ci, ci->left[k]);
        locals[0] = new_local(w, valtype(lt, n->line, w->file));
        op_u(w, W_LOCAL_SET, (uint64_t)locals[0]);
        chain_at(w, n, ci, ci->right[k]);
        locals[1] = new_local(w, valtype(rt, n->line, w->file));
        op_u(w, W_LOCAL_SET, (uint64_t)locals[1]);
        dispatch_values(w, n, call, ts, locals, 2, call->type);

        return;
    }

    if (call->target && call->target->fn && is_nested_fn(call->target->fn)) {
        unsupported(w, n, "this operator");
    }

    if (chain_bridge(w, n, ci, k, lt, rt)) {
        return;
    }

    const bool f = call->op && float_cop(call->op);
    const SType* p0 = call->target && call->target->nparams > 0 ? call->target->params[0] : NULL;
    const SType* p1 = call->target && call->target->nparams > 1 ? call->target->params[1] : NULL;
    if (call->target) {
        i32c(w, 0);
    }

    chain_at(w, n, ci, ci->left[k]);
    if ((f || (p0 && p0->kind == ST_FLOAT)) && lt->kind == ST_INT) {
        op(w, W_F64_CONVERT_I64_S);
    }

    if (p0 && !f) {
        convert(w, lt, p0);
    }

    chain_at(w, n, ci, ci->right[k]);
    if ((f || (p1 && p1->kind == ST_FLOAT)) && rt->kind == ST_INT) {
        op(w, W_F64_CONVERT_I64_S);
    } else if (p1 && !f) {
        convert(w, rt, p1);
    }

    if (call->op) {
        cop(w, call->op, n->line);

        return;
    }

    if (call->target) {
        op_u(w, W_CALL, wfunc_for(call->target)->index);

        return;
    }

    wfail(n->line, w->file, "the wasm target cannot compile operator '%s' here", o);
}

// an operator of a chain that a native of the host does (==, != on composite values, in, ...)
static bool chain_bridge(WCtx* w, Node* n, const ChainInfo* ci, int k, const SType* lt, const SType* rt) {
    const CCall* call = &ci->calls[k];
    const char* key = call->func && call->func->kind == CF_NATIVE && !call->op ? native_key(call->func) : NULL;
    if (!key) {
        return false;
    }

    const int buf = bridge_buf(w, 2);
    SType* ts[2] = {(SType*)lt, (SType*)rt};
    chain_at(w, n, ci, ci->left[k]);
    bridge_store(w, buf, 0, lt, n->line);
    chain_at(w, n, ci, ci->right[k]);
    bridge_store(w, buf, 1, rt, n->line);
    bridge_call(w, buf, key, ts, 2, call->type, n->line);

    return true;
}

static void branchy(WCtx* w, Node* n, bool value);

// ---------------------------------------------------------------- generators: pausing and resuming
//
// A generator is [i32 step][i32 state][8 value][frame: 8 per local of step]. step is the table slot
// of its step function, (generator) -> i32, which runs the body to its next value: 1 when it gave
// one (in the value slot), 0 when the body ended. state: 0 not started, k > 0 paused at pause k,
// -1 done, -2 running. The step function's locals live in the frame between steps: loaded when
// it starts, stored when it pauses.
//
// The pauses are the emits of the body (not of functions nested in it), numbered in the order of
// the source. Resuming at pause k enters the statements on the way to that emit again without
// running anything else: in a block that contains pauses every statement is guarded (run it when
// not resuming, or when the pause is in it), a loop skips its test and its next item, an if or a
// match takes the branch with the pause without testing anything. The emit itself clears the
// resume and the body goes on after it.

#define GEN_DONE_STATE (-1)
#define GEN_RUNNING_STATE (-2)

static int frame_slot(int local) {
    return 16 + 8 * (local - 1); // local 0 is the generator
}

static bool is_generator_type(const SType* t) {
    return t && t->kind == ST_NATIVE && t->decl && t->decl->which == NT_GENERATOR;
}

// the generator (its address in local g) runs to its next value: i32 1 when it gave one
static void gen_advance(WCtx* w, int g) {
    op_u(w, W_LOCAL_GET, (uint64_t)g);
    op_u(w, W_LOCAL_GET, (uint64_t)g);
    mem(w, W_I32_LOAD, 2, 0);
    op_u(w, W_CALL_INDIRECT, (uint64_t)func_type("i", "i"));
    put(w->b, 0);
}

// the statements that can hold a pause: the ones a resume enters (an emit elsewhere, in a value,
// cannot be resumed at)
static void pauses_walk(Node* s, void (*fn)(Node*, void*), void* data) {
    if (!s) {
        return;
    }

    switch (s->kind) {
        case S_RETURN:
            if (s->as.ret.emit) {
                fn(s, data);
            }

            return;
        case N_BLOCK:
            for (int i = 0; i < s->as.block.stmts.count; ++i) {
                pauses_walk(s->as.block.stmts.items[i], fn, data);
            }

            return;
        case S_WHILE: pauses_walk(s->as.while_.body, fn, data); return;
        case S_FOR: pauses_walk(s->as.for_.body, fn, data); return;
        case N_IF:
            pauses_walk(s->as.if_.then_b, fn, data);
            pauses_walk(s->as.if_.else_b, fn, data);

            return;
        case N_MATCH:
            for (int i = 0; i < s->as.match.narms; ++i) {
                if (s->as.match.arms[i].body_is_block) {
                    pauses_walk(s->as.match.arms[i].body, fn, data);
                }
            }

            return;
        case S_EXPR:
            if (s->as.expr.expr->kind == N_IF || s->as.expr.expr->kind == N_MATCH) {
                pauses_walk(s->as.expr.expr, fn, data);
            }

            return;
        default: return;
    }
}

static void pause_collect(Node* s, void* data) {
    WCtx* w = data;
    GROW(w->pauses, w->npauses, w->cappauses);
    w->pauses[w->npauses++] = s;
}

static int pause_id(const WCtx* w, const Node* s) {
    for (int i = 0; i < w->npauses; ++i) {
        if (w->pauses[i] == s) {
            return i + 1;
        }
    }

    return 0;
}

typedef struct {
    const WCtx* w;
    int lo;
    int hi;
} PauseSpan;

static void span_add(Node* s, void* data) {
    PauseSpan* sp = data;
    const int k = pause_id(sp->w, s);
    if (k) {
        if (!sp->lo || k < sp->lo) {
            sp->lo = k;
        }

        if (k > sp->hi) {
            sp->hi = k;
        }
    }
}

// the pauses in s (they are consecutive): false when it has none (or w is no generator)
static bool pause_span(const WCtx* w, Node* s, int* lo, int* hi) {
    if (!w->gen || !s) {
        return false;
    }

    PauseSpan sp = {w, 0, 0};
    pauses_walk(s, span_add, &sp);
    *lo = sp.lo;
    *hi = sp.hi;

    return sp.lo > 0;
}

// i32: not resuming
static void gen_running(WCtx* w) {
    op_u(w, W_LOCAL_GET, (uint64_t)w->gen_resume);
    op(w, W_I32_EQZ);
}

// i32: resuming at a pause in s
static void gen_resumes_in(WCtx* w, Node* s) {
    int lo, hi;
    if (!pause_span(w, s, &lo, &hi)) {
        i32c(w, 0);

        return;
    }

    op_u(w, W_LOCAL_GET, (uint64_t)w->gen_resume);
    i32c(w, lo);
    op(w, W_I32_SUB);
    i32c(w, hi - lo);
    op(w, W_I32_LE_U);
}

// a statement of a block with pauses: if (not resuming || resuming in it) { ... }
static void gen_guard(WCtx* w, Node* s) {
    gen_running(w);
    int lo, hi;
    if (pause_span(w, s, &lo, &hi)) {
        gen_resumes_in(w, s);
        op(w, W_I32_OR);
    }

    open_block(w, W_IF, 0);
}

// the generator's value slot <- the value of e
static void gen_store_value(WCtx* w, Node* e) {
    op_u(w, W_LOCAL_GET, 0);
    wexpr_to(w, e, w->gen_elem);
    store_slot(w, w->gen_elem, 8, e->line);
}

static void gen_set_state(WCtx* w, int state) {
    op_u(w, W_LOCAL_GET, 0);
    i32c(w, state);
    mem(w, W_I32_STORE, 2, 4);
}

// emit v: pauses (unless resumed here, then it goes on); return v: the last value; return: done
static void gen_return(WCtx* w, Node* n) {
    if (n->as.ret.emit) {
        const int k = pause_id(w, n);
        if (!k) {
            unsupported(w, n, "an emit inside an expression (an if or a match whose value is used)");
        }

        gen_running(w);
        open_block(w, W_IF, 0);
        gen_store_value(w, n->as.ret.value);
        gen_set_state(w, k);
        op_u(w, W_BR, (uint64_t)(w->labels - w->gen_exit));
        close_block(w);
        i32c(w, 0);
        op_u(w, W_LOCAL_SET, (uint64_t)w->gen_resume);

        return;
    }

    if (n->as.ret.value) {
        gen_store_value(w, n->as.ret.value);
        gen_set_state(w, GEN_DONE_STATE);
        op_u(w, W_BR, (uint64_t)(w->labels - w->gen_exit));

        return;
    }

    gen_set_state(w, GEN_DONE_STATE);
    i32c(w, 0);
    op(w, W_RETURN);
}

static void block_body(WCtx* w, Node* block, bool value, const SType* want) {
    int lo, hi;
    const bool routed = pause_span(w, block, &lo, &hi);
    begin(w);
    const int count = block->as.block.stmts.count;
    for (int i = 0; i < count; ++i) {
        Node* s = block->as.block.stmts.items[i];
        if (routed) {
            if (value && i == count - 1 && s->kind == S_EXPR && s->as.expr.yield) {
                unsupported(w, s, "a block that gives a value and holds an emit");
            }

            gen_guard(w, s);
            wstmt(w, s);
            close_block(w);

            continue;
        }

        if (value && i == count - 1 && s->kind == S_EXPR && s->as.expr.yield) {
            Node* e = s->as.expr.expr;
            if (e->kind == N_IF || e->kind == N_MATCH) {
                branchy(w, e, true);
            } else {
                wexpr_to(w, e, want);
            }

            continue;
        }

        wstmt(w, s);
    }

    finish(w);
}

static SType* field_type(SType* want, ObjString* name) {
    if (!want || want->kind != ST_RECORD) {
        return NULL;
    }

    st_record_fill(G->ch, want);
    for (int i = 0; i < want->nfields; ++i) {
        if (str_equals(want->fnames[i], name->chars)) {
            return want->ftypes[i];
        }
    }

    return NULL;
}

// a record literal; want: the record type it becomes (its fields' types), or NULL
static void record(WCtx* w, Node* n, SType* want) {
    // the shape: field names and types, so the host can print the record and find its fields
    const int nf = n->as.object.values.count;
    while (G->data.len % 8) {
        put(&G->data, 0);
    }

    const int shape = DATA_BASE + (int)G->data.len;
    const uint32_t count = (uint32_t)nf;
    put_all(&G->data, &count, 4);
    for (int i = 0; i < nf; ++i) {
        const uint32_t id = (uint32_t)name_id(n->as.object.keys[i]);
        SType* ft = field_type(want, n->as.object.keys[i]);
        const uint32_t td = (uint32_t)type_desc(ft ? ft : wtype(w, n->as.object.values.items[i]));
        put_all(&G->data, &id, 4);
        put_all(&G->data, &td, 4);
    }

    const int p = new_local(w, T_I32);
    alloc(w, 8 + 8 * nf);
    op_u(w, W_LOCAL_SET, p);
    op_u(w, W_LOCAL_GET, p);
    i32c(w, shape);
    mem(w, W_I32_STORE, 2, 0);
    for (int i = 0; i < nf; ++i) {
        Node* v = n->as.object.values.items[i];
        SType* ft = field_type(want, n->as.object.keys[i]);
        op_u(w, W_LOCAL_GET, p);
        wexpr_to(w, v, ft);
        store_slot(w, ft ? ft : wtype(w, v), 8 + 8 * i, n->line);
    }

    op_u(w, W_LOCAL_GET, p);
}

static void array(WCtx* w, Node* n) {
    const SType* t = wtype(w, n);
    const int count = n->as.array.items.count;
    const int cap = count ? count : 4;
    const int p = new_local(w, T_I32);
    alloc(w, 16);
    op_u(w, W_LOCAL_SET, p);
    op_u(w, W_LOCAL_GET, p);
    i32c(w, count);
    mem(w, W_I32_STORE, 2, 0);
    op_u(w, W_LOCAL_GET, p);
    i32c(w, cap);
    mem(w, W_I32_STORE, 2, 4);
    op_u(w, W_LOCAL_GET, p);
    alloc(w, 8 * cap);
    mem(w, W_I32_STORE, 2, 8);
    for (int i = 0; i < count; ++i) {
        op_u(w, W_LOCAL_GET, p);
        mem(w, W_I32_LOAD, 2, 8);
        wexpr_to(w, n->as.array.items.items[i], t->elem);
        store_slot(w, t->elem, 8 * i, n->line);
    }

    op_u(w, W_LOCAL_GET, p);
}

static void wexpr(WCtx* w, Node* n) {
    switch (n->kind) {
        case N_INT:
            op(w, W_I64_CONST);
            sleb(w->b, n->as.ival);

            return;
        case N_FLOAT:
            op(w, W_F64_CONST);
            put_all(w->b, &n->as.fval, 8);

            return;
        case N_STRING: i32c(w, str_addr(n->as.str)); return;
        case N_BOOL: i32c(w, n->as.bval ? 1 : 0); return;
        case N_IDENT: {
            SType* t = wtype(w, n);
            if (t->kind == ST_OVERLOAD) {
                function_value(w, n, t);

                return;
            }

            if (!load_var(w, n->as.str)) {
                wfail(n->line, w->file, "the wasm target cannot use '%s' as a value here", n->as.str->chars);
            }

            return;
        }
        case N_ARRAY: array(w, n); return;
        case N_OBJECT: record(w, n, NULL); return;
        case N_CALL: wcall(w, n, &winfo(w, n)->call, NULL, &n->as.call.args, n->as.call.callee); return;
        case N_METHOD: wcall(w, n, &winfo(w, n)->call, n->as.method.recv, &n->as.method.args, n->as.method.callee); return;
        case N_FIELD: {
            const SType* ot = wtype(w, n->as.field.obj);
            if (ot->kind == ST_RANGE) {
                if (str_equals(n->as.field.name, "end")) {
                    // the end as written: a ...= range stores it one step further, so end - step * inclusive
                    const int r = new_local(w, T_I32);
                    wexpr(w, n->as.field.obj);
                    op_u(w, W_LOCAL_TEE, (uint64_t)r);
                    mem(w, W_I64_LOAD, 3, 8);
                    op_u(w, W_LOCAL_GET, (uint64_t)r);
                    mem(w, W_I64_LOAD, 3, 16);
                    op_u(w, W_LOCAL_GET, (uint64_t)r);
                    mem(w, W_I32_LOAD, 2, 24);
                    op(w, W_I64_EXTEND_I32_S);
                    op(w, W_I64_MUL);
                    op(w, W_I64_SUB);

                    return;
                }

                static const char* fields[] = {"start", "end", "step"};
                for (int i = 0; i < 3; ++i) {
                    if (str_equals(n->as.field.name, fields[i])) {
                        wexpr(w, n->as.field.obj);
                        mem(w, W_I64_LOAD, 3, 8 * i);

                        return;
                    }
                }

                wexpr(w, n->as.field.obj);
                mem(w, W_I32_LOAD, 2, 24);

                return;
            }

            if (ot->kind == ST_TYPE && ot->decl && ot->decl->kind == TD_ENUM) {
                // Color:RED: a variant without payload
                for (int i = 0; i < ot->decl->nvariants; ++i) {
                    if (str_equals(ot->decl->variants[i].name, n->as.field.name->chars)) {
                        CFunc f = {0};
                        f.variant = i;
                        NodeList none = {0};
                        make_variant(w, n, &f, NULL, &none);

                        return;
                    }
                }
            }

            if (ot->kind == ST_MODULE) {
                // module:name: a global of the module, or a constant of a native module
                CModule* m = ot->module;
                ObjString* src = n->as.field.name;
                for (int i = 0; i < m->nexports; ++i) {
                    if (str_equals(m->exp_names[i], src->chars)) {
                        src = m->exp_src[i];
                    }
                }

                if (m->rt) {
                    Value v;
                    if (!module_member(m->rt, n->as.field.name, &v)) {
                        wfail(n->line, w->file, "module '%s' has no constant '%s'", m->name->chars, src->chars);
                    }

                    if (IS_KIND(v, OBJ_STRING)) {
                        i32c(w, str_addr(AS_STR(v)));
                    } else if (v.kind == VAL_INT) {
                        op(w, W_I64_CONST);
                        sleb(w->b, v.as.i);
                    } else if (v.kind == VAL_FLOAT) {
                        op(w, W_F64_CONST);
                        put_all(w->b, &v.as.f, 8);
                    } else if (v.kind == VAL_BOOL) {
                        i32c(w, v.as.b ? 1 : 0);
                    } else {
                        wfail(n->line, w->file, "the wasm target cannot use '%s:%s' as a value", m->name->chars, src->chars);
                    }

                    return;
                }

                module_init(m);
                const WGlobal* g = find_global(m, src);
                if (!g) {
                    wfail(n->line, w->file, "the wasm target can read only the variables of a module, not '%s'", src->chars);
                }

                op_u(w, W_GLOBAL_GET, g->index);

                return;
            }

            if (ot->kind != ST_RECORD) {
                wfail(n->line, w->file, "the wasm target cannot read a field of a value of type %s", st_name(ot));
            }

            wexpr(w, n->as.field.obj);
            i32c(w, name_id(n->as.field.name));
            host(w, H_FIELD);
            load_slot(w, wtype(w, n), 0, n->line);

            return;
        }
        case N_INDEX: {
            NodeList keys = n->as.index.args;
            wcall(w, n, &winfo(w, n)->call, n->as.index.obj, &keys, NULL);

            return;
        }
        case N_CHAIN: {
            const ChainInfo* ci = winfo(w, n)->chain;
            chain_at(w, n, ci, ci->root);

            return;
        }
        case N_IF:
        case N_MATCH: branchy(w, n, true); return;
        case N_BLOCK: block_body(w, n, true, wtype(w, n)); return;
        case N_FUNC: make_closure(w, winfo(w, n)->call.target, n->line); return;
        case N_SCOPECALL: scope_call(w, n, true); return;
        case N_AS: as_value(w, n); return;
        default: unsupported(w, n, "this kind of expression");
    }
}



// ---------------------------------------------------------------- patterns, if, match

static CFunc* ctor_named(WCtx* w, ObjString* name) {
    const CScope* s = w->inst->fn && w->inst->fn->closure ? w->inst->fn->closure : w->module->scope;
    const CSym* sym = scope_lookup((CScope*)s, name);
    if (!sym) {
        sym = scope_lookup(G->ch->builtins, name);
    }

    for (int i = 0; sym && sym->kind == SYM_FUNCS && i < sym->funcs->count; ++i) {
        if (sym->funcs->items[i]->kind == CF_CTOR) {
            return sym->funcs->items[i];
        }
    }

    return NULL;
}

// tests the value in local `slot` (of type t) against p; a failure branches out of the block `fail`
static void pattern(WCtx* w, Pattern* p, int slot, SType* t, int fail) {
    switch (p->kind) {
        case P_WILD: return;
        case P_LITERAL: {
            op_u(w, W_LOCAL_GET, slot);
            wexpr_to(w, p->lit, t);
            const STKind k = t->kind;
            if (k == ST_INT) {
                op(w, W_I64_EQ);
            } else if (k == ST_FLOAT) {
                op(w, W_F64_EQ);
            } else if (k == ST_BOOL) {
                op(w, W_I32_EQ);
            } else if (k == ST_STRING) {
                host(w, H_STR_EQ);
            } else {
                wfail(p->line, w->file, "the wasm target compares only numbers, bools and strings in patterns");
            }

            op(w, W_I32_EQZ);
            op_u(w, W_BR_IF, (uint64_t)(w->labels - fail));

            return;
        }
        case P_BIND: {
            CFunc* unit = ctor_named(w, p->name);
            if (unit && unit->en->variants[unit->variant].npayload == 0) {
                op_u(w, W_LOCAL_GET, slot);
                mem(w, W_I32_LOAD, 2, 0);
                i32c(w, unit->variant);
                op(w, W_I32_NE);
                op_u(w, W_BR_IF, (uint64_t)(w->labels - fail));

                return;
            }

            op_u(w, W_LOCAL_GET, slot);
            declare(w, p->name, t, p->line);

            return;
        }
        case P_VARIANT: {
            Node* cn = p->ctor;
            if (cn->kind == N_IDENT && str_equals(cn->as.str, "tuple")) {
                if (t->kind == ST_TUPLE && p->nsubs != t->nargs) {
                    op_u(w, W_BR, (uint64_t)(w->labels - fail)); // another length: it never matches

                    return;
                }

                for (int i = 0; i < p->nsubs && t->kind == ST_TUPLE; ++i) {
                    const uint8_t vt = valtype(t->args[i], p->line, w->file);
                    const int s = new_local(w, vt);
                    op_u(w, W_LOCAL_GET, slot);
                    load_slot(w, t->args[i], 8 * i, p->line);
                    op_u(w, W_LOCAL_SET, s);
                    pattern(w, p->subs[i], s, t->args[i], fail);
                }

                return;
            }

            int variant = -1;
            if (cn->kind == N_IDENT) {
                CFunc* f = ctor_named(w, cn->as.str);
                variant = f ? f->variant : -1;
            } else if (cn->kind == N_FIELD && t->kind == ST_ENUM) {
                for (int i = 0; i < t->decl->nvariants; ++i) {
                    if (str_equals(t->decl->variants[i].name, cn->as.field.name->chars)) {
                        variant = i;
                    }
                }
            }

            if (variant < 0 || t->kind != ST_ENUM) {
                wfail(p->line, w->file, "the wasm target does not understand this pattern");
            }

            op_u(w, W_LOCAL_GET, slot);
            mem(w, W_I32_LOAD, 2, 0);
            i32c(w, variant);
            op(w, W_I32_NE);
            op_u(w, W_BR_IF, (uint64_t)(w->labels - fail));
            for (int i = 0; i < p->nsubs; ++i) {
                SType* pt = variant_payload(G->ch, t, variant, i);
                const uint8_t vt = valtype(pt, p->line, w->file);
                if (!vt) {
                    continue;
                }

                const int s = new_local(w, vt);
                op_u(w, W_LOCAL_GET, slot);
                load_slot(w, pt, 8 + 8 * i, p->line);
                op_u(w, W_LOCAL_SET, s);
                pattern(w, p->subs[i], s, pt, fail);
            }

            return;
        }
    }
}

static void branch_body(WCtx* w, Node* body, bool is_block, bool value, const SType* t) {
    if (is_block) {
        block_body(w, body, value, t);
    } else {
        wexpr_to(w, body, t);
        if (!value && valtype(wtype(w, body), body->line, w->file)) {
            op(w, W_DROP);
        }
    }
}

static void branchy(WCtx* w, Node* n, bool value) {
    const SType* t = value ? wtype(w, n) : NULL;
    const uint8_t rt = value ? valtype(t, n->line, w->file) : 0;
    int lo, hi;
    // in a generator, resuming at a pause in it: the branch with the pause, nothing tested
    const bool routed = !value && pause_span(w, n, &lo, &hi);
    if (n->kind == N_IF && !n->as.if_.pat) {
        if (routed) {
            gen_running(w);
            open_block(w, W_IF, T_I32);
            wexpr(w, n->as.if_.cond);
            op(w, W_ELSE);
            gen_resumes_in(w, n->as.if_.then_b);
            close_block(w);
        } else {
            wexpr(w, n->as.if_.cond);
        }

        open_block(w, W_IF, rt);
        block_body(w, n->as.if_.then_b, value, t);
        op(w, W_ELSE);
        if (n->as.if_.else_b) {
            Node* e = n->as.if_.else_b;
            if (e->kind == N_IF || e->kind == N_MATCH) {
                branchy(w, e, value);
            } else {
                block_body(w, e, value, t);
            }
        }

        close_block(w);

        return;
    }

    // block $end (result) { block $arm { tests; bindings; body; br $end } ... no match }
    open_block(w, W_BLOCK, rt);
    const int end = w->labels;
    begin(w);
    if (n->kind == N_IF) {
        SType* st = wtype(w, n->as.if_.cond);
        const int s = new_local(w, valtype(st, n->line, w->file));
        if (routed) {
            gen_running(w);
            open_block(w, W_IF, 0);
        }

        wexpr(w, n->as.if_.cond);
        op_u(w, W_LOCAL_SET, s);
        if (routed) {
            close_block(w);
        }

        open_block(w, W_BLOCK, 0);
        const int fail = w->labels;
        begin(w);
        if (routed) {
            gen_running(w);
            open_block(w, W_IF, 0);
            pattern(w, n->as.if_.pat, s, st, fail);
            op(w, W_ELSE);
            gen_resumes_in(w, n->as.if_.then_b);
            op(w, W_I32_EQZ);
            op_u(w, W_BR_IF, (uint64_t)(w->labels - fail));
            close_block(w);
        } else {
            pattern(w, n->as.if_.pat, s, st, fail);
        }

        block_body(w, n->as.if_.then_b, value, t);
        finish(w);
        op_u(w, W_BR, (uint64_t)(w->labels - end));
        close_block(w);
        if (n->as.if_.else_b) {
            Node* e = n->as.if_.else_b;
            if (e->kind == N_IF || e->kind == N_MATCH) {
                branchy(w, e, value);
            } else {
                block_body(w, e, value, t);
            }
        }
    } else {
        SType* st = wtype(w, n->as.match.subject);
        const int s = new_local(w, valtype(st, n->line, w->file));
        if (routed) {
            gen_running(w);
            open_block(w, W_IF, 0);
        }

        wexpr(w, n->as.match.subject);
        op_u(w, W_LOCAL_SET, s);
        if (routed) {
            close_block(w);
        }

        for (int i = 0; i < n->as.match.narms; ++i) {
            MatchArm* arm = &n->as.match.arms[i];
            open_block(w, W_BLOCK, 0);
            const int fail = w->labels;
            begin(w);
            if (routed) {
                gen_running(w);
                open_block(w, W_IF, 0);
            }

            pattern(w, arm->pat, s, st, fail);
            if (arm->guard) {
                wexpr(w, arm->guard);
                op(w, W_I32_EQZ);
                op_u(w, W_BR_IF, (uint64_t)(w->labels - fail));
            }

            if (routed) {
                op(w, W_ELSE);
                if (arm->body_is_block) {
                    gen_resumes_in(w, arm->body);
                } else {
                    i32c(w, 0);
                }

                op(w, W_I32_EQZ);
                op_u(w, W_BR_IF, (uint64_t)(w->labels - fail));
                close_block(w);
            }

            branch_body(w, arm->body, arm->body_is_block, value, t);
            finish(w);
            op_u(w, W_BR, (uint64_t)(w->labels - end));
            close_block(w);
        }

        i32c(w, n->line);
        host(w, H_NO_MATCH);
        op(w, W_UNREACHABLE);
    }

    finish(w);
    close_block(w);
}



// ---------------------------------------------------------------- statements

// for over an array, an int range or a generator; a value of a type that implements Iterable is
// iterated through the generator the prelude's iterate($for) makes of it
static void for_loop(WCtx* w, Node* n) {
    SType* it = wtype(w, n->as.for_.iter);
    const bool over_gen = n->as.for_.proto || is_generator_type(it);
    if (!over_gen) {
        if (it->kind == ST_RANGE && it->elem && it->elem->kind == ST_FLOAT) {
            unsupported(w, n, "a for loop over a float range");
        }

        if (it->kind != ST_RANGE && it->kind != ST_ARRAY && it->kind != ST_STRING && !is_collection_type(it)) {
            unsupported(w, n, "a for loop over this kind of value");
        }
    }

    int lo, hi;
    const bool routed = pause_span(w, n, &lo, &hi); // a generator resumed in the body skips the start and the test
    begin(w);
    const int coll = new_local(w, T_I32);
    const int pos = new_local(w, T_I64);
    const int idx = new_local(w, T_I64);
    const bool range = !over_gen && it->kind == ST_RANGE;
    const int end = range ? new_local(w, T_I64) : -1;
    const int step = range ? new_local(w, T_I64) : -1;
    SType* gt = it;
    if (routed) {
        gen_running(w);
        open_block(w, W_IF, 0);
    }

    wexpr(w, n->as.for_.iter);
    if (!over_gen && it->kind == ST_STRING) {
        // its characters, as an array of strings
        const int buf = bridge_buf(w, 1);
        bridge_store(w, buf, 0, it, n->line);
        SType* ts[1] = {it};
        bridge_call(w, buf, "chars", ts, 1, st_array(st_prim(ST_STRING)), n->line);
    }

    SType* entries = NULL; // a map: its entries, tuple(key, value)
    if (!over_gen && is_collection_type(it)) {
        // a snapshot: the items of a set, the entries of a map, as an array
        const bool map = it->decl->which == NT_MAP;
        if (it->nargs < (map ? 2 : 1)) {
            unsupported(w, n, "a for loop over a Map / Set whose types are not known");
        }

        SType* et = it->args[0];
        if (map) {
            SType** kv = pp_calloc(2, sizeof(SType*));
            kv[0] = it->args[0];
            kv[1] = it->args[1];
            entries = st_new(ST_TUPLE);
            entries->args = kv;
            entries->nargs = 2;
            et = entries;
        }

        const int buf = bridge_buf(w, 1);
        bridge_store(w, buf, 0, it, n->line);
        SType* ts[1] = {it};
        bridge_call(w, buf, map ? "std_map_entries" : "std_map_keys", ts, 1, st_array(et), n->line);
    }

    if (n->as.for_.proto) {
        begin(w);
        declare(w, str_cintern(G->vm, "$for"), it, n->line);
        wexpr(w, n->as.for_.proto);
        gt = wtype(w, n->as.for_.proto);
        finish(w);
        if (!is_generator_type(gt)) {
            unsupported(w, n, "iterating through a protocol call that does not give a Generator");
        }
    }

    op_u(w, W_LOCAL_SET, coll);
    op(w, W_I64_CONST);
    sleb(w->b, 0);
    op_u(w, W_LOCAL_SET, pos);
    op(w, W_I64_CONST);
    sleb(w->b, 0);
    op_u(w, W_LOCAL_SET, idx);
    if (range) {
        op_u(w, W_LOCAL_GET, coll);
        mem(w, W_I64_LOAD, 3, 0);
        op_u(w, W_LOCAL_SET, pos);
        op_u(w, W_LOCAL_GET, coll);
        mem(w, W_I64_LOAD, 3, 8);
        op_u(w, W_LOCAL_SET, end);
        op_u(w, W_LOCAL_GET, coll);
        mem(w, W_I64_LOAD, 3, 16);
        op_u(w, W_LOCAL_SET, step);
    }

    if (routed) {
        close_block(w);
    }

    SType* item = over_gen ? (gt->nargs ? gt->args[0] : NULL)
                  : range                    ? st_prim(ST_INT)
                  : it->kind == ST_STRING    ? it
                  : entries                  ? entries
                  : is_collection_type(it)   ? it->args[0]
                                             : it->elem;
    if (!item) {
        unsupported(w, n, "iterating a generator whose values have no type");
    }

    if (over_gen && n->as.for_.var2 && (item->kind != ST_TUPLE || item->nargs != 2)) {
        unsupported(w, n, "for k, v over a generator whose values are not tuple(key, value)");
    }

    open_block(w, W_BLOCK, 0);
    const int exit = w->labels;
    open_block(w, W_LOOP, 0);
    const int top = w->labels;
    if (routed) {
        gen_running(w);
        open_block(w, W_IF, 0);
    }

    // the end: the generator gave nothing, step > 0 ? pos >= end : pos <= end, or pos >= #array
    if (over_gen) {
        gen_advance(w, coll);
        op(w, W_I32_EQZ);
    } else if (range) {
        op_u(w, W_LOCAL_GET, step);
        op(w, W_I64_CONST);
        sleb(w->b, 0);
        op(w, W_I64_GT_S);
        open_block(w, W_IF, T_I32);
        op_u(w, W_LOCAL_GET, pos);
        op_u(w, W_LOCAL_GET, end);
        op(w, W_I64_GE_S);
        op(w, W_ELSE);
        op_u(w, W_LOCAL_GET, pos);
        op_u(w, W_LOCAL_GET, end);
        op(w, W_I64_LE_S);
        close_block(w);
    } else {
        op_u(w, W_LOCAL_GET, pos);
        op_u(w, W_LOCAL_GET, coll);
        mem(w, W_I32_LOAD, 2, 0);
        op(w, W_I64_EXTEND_I32_S);
        op(w, W_I64_GE_S);
    }

    op_u(w, W_BR_IF, (uint64_t)(w->labels - exit));
    begin(w);
    if ((over_gen || entries) && n->as.for_.var2) {
        // tuple(key, value)
        const int tup = new_local(w, T_I32);
        if (over_gen) {
            op_u(w, W_LOCAL_GET, coll);
            mem(w, W_I32_LOAD, 2, 8);
        } else {
            op_u(w, W_LOCAL_GET, coll);
            mem(w, W_I32_LOAD, 2, 8);
            op_u(w, W_LOCAL_GET, pos);
            op(w, W_I32_WRAP_I64);
            i32c(w, 3);
            op(w, W_I32_SHL);
            op(w, W_I32_ADD);
            mem(w, W_I32_LOAD, 2, 0);
        }

        op_u(w, W_LOCAL_TEE, tup);
        load_slot(w, item->args[0], 0, n->line);
        declare(w, n->as.for_.var1, item->args[0], n->line);
        op_u(w, W_LOCAL_GET, tup);
        load_slot(w, item->args[1], 8, n->line);
        declare(w, n->as.for_.var2, item->args[1], n->line);
    } else if (over_gen) {
        op_u(w, W_LOCAL_GET, coll);
        load_slot(w, item, 8, n->line);
        declare(w, n->as.for_.var1, item, n->line);
    } else {
        if (n->as.for_.var2) {
            op_u(w, W_LOCAL_GET, idx);
            declare(w, n->as.for_.var1, st_prim(ST_INT), n->line);
        }

        if (range) {
            op_u(w, W_LOCAL_GET, pos);
        } else {
            op_u(w, W_LOCAL_GET, coll);
            mem(w, W_I32_LOAD, 2, 8);
            op_u(w, W_LOCAL_GET, pos);
            op(w, W_I32_WRAP_I64);
            i32c(w, 3);
            op(w, W_I32_SHL);
            op(w, W_I32_ADD);
            load_slot(w, item, 0, n->line);
        }

        declare(w, n->as.for_.var2 ? n->as.for_.var2 : n->as.for_.var1, item, n->line);
    }

    if (routed) {
        close_block(w);
    }

    open_block(w, W_BLOCK, 0);
    w->loops[w->nloops].exit_depth = exit;
    w->loops[w->nloops].cont_depth = w->labels;
    w->nloops++;
    block_body(w, n->as.for_.body, false, NULL);
    w->nloops--;
    close_block(w);
    finish(w);
    if (!over_gen) {
        // the next one
        op_u(w, W_LOCAL_GET, pos);
        if (range) {
            op_u(w, W_LOCAL_GET, step);
        } else {
            op(w, W_I64_CONST);
            sleb(w->b, 1);
        }

        op(w, W_I64_ADD);
        op_u(w, W_LOCAL_SET, pos);
        op_u(w, W_LOCAL_GET, idx);
        op(w, W_I64_CONST);
        sleb(w->b, 1);
        op(w, W_I64_ADD);
        op_u(w, W_LOCAL_SET, idx);
    }

    op_u(w, W_BR, (uint64_t)(w->labels - top));
    close_block(w);
    close_block(w);
    finish(w);
}

// the value on the stack (of type t) in a new hidden variable, and a name node that reads it: a
// call made of values computed once (c[k] op= v reads and stores c[k] with the same c and k)
static Node* temp_value(WCtx* w, SType* t, int line) {
    static int count = 0;
    char name[32];
    snprintf(name, sizeof name, "\x01tmp%d", ++count);
    Node* id = pp_calloc(1, sizeof(Node));
    id->kind = N_IDENT;
    id->line = line;
    id->as.str = str_cintern(G->vm, name);
    info_put(w->inst, id)->type = t;
    const uint8_t vt = valtype(t, line, w->file);
    const int idx = add_named(w, id->as.str, vt ? vt : T_I32);
    w->locals[w->nlocals - 1].type = t;
    op_u(w, W_LOCAL_SET, (uint64_t)idx);

    return id;
}

static void list_add(NodeList* l, Node* n) {
    GROW(l->items, l->count, l->cap);
    l->items[l->count++] = n;
}

// c[k] = v / c[k] op= v on a value that is not an array: calls of [] and []= (Index)
static void index_assign(WCtx* w, Node* n) {
    Node* target = n->as.assign.target;
    Node* value = n->as.assign.value;
    const NodeInfo* ni = winfo(w, n);
    NodeList args = {0};
    if (!n->as.assign.op) {
        for (int i = 0; i < target->as.index.args.count; ++i) {
            list_add(&args, target->as.index.args.items[i]);
        }

        list_add(&args, value);
        wcall(w, n, &ni->call, target->as.index.obj, &args, NULL);
        free(args.items);

        return;
    }

    const CCall* op_call = &ni->call;
    const CCall* store = &ni->call2;
    if (!store->func && !store->target && !store->dispatch) {
        unsupported(w, n, "this compound assignment");
    }

    // c, k once; the current value through [], then op, then []=
    begin(w);
    const SType* tt = wtype(w, target);
    wexpr(w, target->as.index.obj);
    Node* obj = temp_value(w, wtype(w, target->as.index.obj), n->line);
    for (int i = 0; i < target->as.index.args.count; ++i) {
        Node* k = target->as.index.args.items[i];
        wexpr(w, k);
        list_add(&args, temp_value(w, wtype(w, k), n->line));
    }

    wcall(w, target, &winfo(w, target)->call, obj, &args, NULL);
    Node* cur = temp_value(w, (SType*)tt, n->line);
    NodeList opargs = {0};
    list_add(&opargs, value);
    wcall(w, n, op_call, cur, &opargs, NULL);
    free(opargs.items);
    SType* rt = op_call->target ? op_call->target->ret : (SType*)tt;
    convert(w, rt, tt);
    list_add(&args, temp_value(w, (SType*)tt, n->line));
    wcall(w, n, store, obj, &args, NULL);
    free(args.items);
    finish(w);
}

static void assign(WCtx* w, Node* n) {
    Node* target = n->as.assign.target;
    Node* value = n->as.assign.value;
    const NodeInfo* ni = winfo(w, n);
    if (target->kind == N_INDEX && wtype(w, target->as.index.obj)->kind != ST_ARRAY) {
        // a []= of the program or of a library (Map through Index), not the built-in one
        const CCall* store = n->as.assign.op ? &ni->call2 : &ni->call;
        const bool builtin = store->func && store->func->kind == CF_NATIVE && store->func->builtin && !store->dispatch;
        if (!builtin && (store->target || store->dispatch || store->func)) {
            index_assign(w, n);

            return;
        }
    }

    if (n->as.assign.op) {
        // target op= value: only on variables and fields, with a built-in operation
        const CCall* call = &ni->call;
        const bool array_slot = target->kind == N_INDEX && target->as.index.args.count == 1 &&
                                wtype(w, target->as.index.obj)->kind == ST_ARRAY;
        if (target->kind == N_INDEX && !array_slot) {
            unsupported(w, n, "this compound assignment");
        }

        const char* key = !call->op && call->func && call->func->kind == CF_NATIVE ? native_key(call->func) : NULL;
        const bool direct = !call->op && call->target && !call->dispatch && !(call->target->fn && is_nested_fn(call->target->fn));
        if (!call->op && !key && !direct) {
            unsupported(w, n, "this compound assignment");
        }

        const SType* tt = wtype(w, target);
        if (array_slot) {
            const int addr = new_local(w, T_I32);
            wexpr(w, target->as.index.obj);
            wexpr(w, target->as.index.args.items[0]);
            host(w, H_ARR_SLOT);
            op_u(w, W_LOCAL_TEE, addr);
            op_u(w, W_LOCAL_GET, addr);
            load_slot(w, tt, 0, n->line);
        } else if (target->kind == N_FIELD) {
            const int addr = new_local(w, T_I32);
            wexpr(w, target->as.field.obj);
            i32c(w, name_id(target->as.field.name));
            host(w, H_FIELD);
            op_u(w, W_LOCAL_TEE, addr);
            op_u(w, W_LOCAL_GET, addr);
            load_slot(w, tt, 0, n->line);
        } else {
            wexpr(w, target);
        }

        if (!call->op) {
            // the current value is on the stack: op(current, value), then stored like below
            SType* ts[2] = {(SType*)tt, wtype(w, value)};
            if (key) {
                const int cur = new_local(w, valtype(tt, n->line, w->file));
                op_u(w, W_LOCAL_SET, (uint64_t)cur);
                const int buf = bridge_buf(w, 2);
                op_u(w, W_LOCAL_GET, (uint64_t)cur);
                bridge_store(w, buf, 0, tt, n->line);
                wexpr(w, value);
                bridge_store(w, buf, 1, ts[1], n->line);
                bridge_call(w, buf, key, ts, 2, (SType*)tt, n->line);
            } else {
                CInstance* t = call->target;
                const int cur = new_local(w, valtype(tt, n->line, w->file));
                op_u(w, W_LOCAL_SET, (uint64_t)cur);
                i32c(w, 0);
                op_u(w, W_LOCAL_GET, (uint64_t)cur);
                convert(w, tt, t->nparams > 0 ? t->params[0] : NULL);
                wexpr_to(w, value, t->nparams > 1 ? t->params[1] : NULL);
                op_u(w, W_CALL, (uint64_t)wfunc_for(t)->index);
                convert(w, t->ret, tt);
            }
        } else {
            const bool f = float_cop(call->op);
            if (f && tt->kind == ST_INT) {
                op(w, W_F64_CONVERT_I64_S);
            }

            wexpr(w, value);
            if (f && wtype(w, value)->kind == ST_INT) {
                op(w, W_F64_CONVERT_I64_S);
            }

            cop(w, call->op, n->line);
        }
        if (target->kind == N_FIELD || array_slot) {
            store_slot(w, tt, 0, n->line);
        } else {
            store_var(w, target->as.str, n->line);
        }

        return;
    }

    if (target->kind == N_IDENT) {
        wexpr_to(w, value, wtype(w, target));
        store_var(w, target->as.str, n->line);

        return;
    }

    if (target->kind == N_FIELD) {
        const SType* tt = wtype(w, target);
        wexpr(w, target->as.field.obj);
        i32c(w, name_id(target->as.field.name));
        host(w, H_FIELD);
        wexpr_to(w, value, tt);
        store_slot(w, tt, 0, n->line);

        return;
    }

    // c[k] = v on an array
    const SType* ot = wtype(w, target->as.index.obj);
    if (ot->kind != ST_ARRAY || target->as.index.args.count != 1 || ni->call.target) {
        unsupported(w, n, "this index assignment");
    }

    wexpr(w, target->as.index.obj);
    wexpr(w, target->as.index.args.items[0]);
    host(w, H_ARR_SLOT);
    wexpr_to(w, value, ot->elem);
    store_slot(w, ot->elem, 0, n->line);
}

static int module_init(CModule* m);

static void wstmt(WCtx* w, Node* n) {
    switch (n->kind) {
        case S_EXPR: {
            Node* e = n->as.expr.expr;
            if (e->kind == N_IF || e->kind == N_MATCH) {
                branchy(w, e, false);

                return;
            }

            if (e->kind == N_SCOPECALL) {
                scope_call(w, e, false);

                return;
            }

            wexpr(w, e);
            if (valtype(wtype(w, e), n->line, w->file)) {
                op(w, W_DROP);
            }

            return;
        }
        case S_LET: {
            SType* t = NULL;
            const NodeInfo* li = n->as.let.init ? winfo(w, n->as.let.init) : NULL;
            if (n->as.let.type) {
                Ctx x = {0};
                x.c = G->ch;
                t = resolve_type(&x, n->as.let.type, w->inst->fn && w->inst->fn->closure ? w->inst->fn->closure : w->module->scope,
                                 &w->inst->bindings);
            } else {
                t = li ? li->type : st_prim(ST_BOOL);
            }

            if (n->as.let.init) {
                wexpr_to(w, n->as.let.init, t);
            } else if (n->as.let.is_const) {
                i32c(w, 1);
            } else {
                const uint8_t vt = valtype(t, n->line, w->file);
                if (vt == T_I64) {
                    op(w, W_I64_CONST);
                    sleb(w->b, 0);
                } else if (vt == T_F64) {
                    const double z = 0;
                    op(w, W_F64_CONST);
                    put_all(w->b, &z, 8);
                } else {
                    i32c(w, 0);
                }
            }

            declare(w, n->as.let.name, t, n->line);

            return;
        }
        case S_ASSIGN: assign(w, n); return;
        case S_FUNCDECL: {
            // a local function: each instance a closure in a cell named after it, so bodies
            // (its own too) can call it; a declaration without a body makes the cell
            const NodeInfo* ni = winfo(w, n);
            CFunc* f = ni ? ni->call.func : NULL;
            if (!f || f->kind != CF_USER || !is_nested_fn(f)) {
                return; // a module's functions are compiled when called
            }

            static SType fn_type = {.kind = ST_FUNCTION};
            for (int i = 0; i < f->ninsts; ++i) {
                char key[32];
                snprintf(key, sizeof key, "\x01fn%d", f->insts[i]->id);
                ObjString* name = str_cintern(G->vm, key);
                if (!find_local(w, name)) {
                    names_add(&w->boxnames, name);
                    i32c(w, 0);
                    declare(w, name, &fn_type, n->line);
                }

                if (n->as.func.body) {
                    make_closure(w, f->insts[i], n->line);
                    store_var(w, name, n->line);
                }
            }

            return;
        }
        case S_TYPE:
        case S_ENUM:
        case S_INTERFACE:
        case S_IMPL:
        case S_PRECEDENCE: return;
        case S_IMPORT: {
            Ctx x = {0};
            x.c = G->ch;
            const char* saved = G->ch->file;
            G->ch->file = w->module->path;
            CModule* m = load_module(&x, n, n->line);
            G->ch->file = saved;
            if (!m->native) {
                op_u(w, W_CALL, module_init(m));
            }

            return;
        }
        case S_EXPORT:
            if (n->as.export_.decl) {
                wstmt(w, n->as.export_.decl);
            }

            return;
        case S_FOR: for_loop(w, n); return;
        case S_WHILE: {
            int lo, hi;
            const bool routed = pause_span(w, n, &lo, &hi);
            open_block(w, W_BLOCK, 0);
            const int exit = w->labels;
            open_block(w, W_LOOP, 0);
            const int top = w->labels;
            if (routed) {
                // resumed in the body: no test
                gen_running(w);
                open_block(w, W_IF, 0);
            }

            wexpr(w, n->as.while_.cond);
            op(w, W_I32_EQZ);
            op_u(w, W_BR_IF, (uint64_t)(w->labels - exit));
            if (routed) {
                close_block(w);
            }

            open_block(w, W_BLOCK, 0);
            w->loops[w->nloops].exit_depth = exit;
            w->loops[w->nloops].cont_depth = w->labels;
            w->nloops++;
            block_body(w, n->as.while_.body, false, NULL);
            w->nloops--;
            close_block(w);
            op_u(w, W_BR, (uint64_t)(w->labels - top));
            close_block(w);
            close_block(w);

            return;
        }
        case S_BREAK: emit_leave_loop(w, false, n->line); return;
        case S_CONTINUE: emit_leave_loop(w, true, n->line); return;
        case S_RETURN:
            if (w->gen) {
                gen_return(w, n);

                return;
            }

            if (n->as.ret.value) {
                wexpr_to(w, n->as.ret.value, w->inst->ret);
                if (!w->inst->fn || !valtype(w->inst->ret, n->line, w->file)) {
                    if (valtype(wtype(w, n->as.ret.value), n->line, w->file)) {
                        op(w, W_DROP);
                    }
                }
            }

            emit_return(w, n->line);

            return;
        case N_BLOCK: block_body(w, n, false, NULL); return;
        case S_DEFER: {
            // the cleanup of the running scope call (this scope function's)
            const int d = new_local(w, T_I32);
            inline_closure(w, n->as.defer.body, WK_DEFER, NULL, w->inst->fn ? w->inst->fn->name : NULL, -1, n->line);
            op_u(w, W_LOCAL_SET, (uint64_t)d);
            op_u(w, W_GLOBAL_GET, (uint64_t)G->g_scope);
            op_u(w, W_LOCAL_GET, (uint64_t)d);
            mem(w, W_I32_STORE, 2, 0);

            return;
        }
        default:
            wexpr(w, n);
            if (valtype(wtype(w, n), n->line, w->file)) {
                op(w, W_DROP);
            }

            return;
    }
}



// ---------------------------------------------------------------- functions and modules

// A generator function: its function makes the generator (the closure and the arguments go into
// the frame); its body is a step function (see "generators: pausing and resuming").
static void compile_generator(WFunc* wf, CModule* m, WCtx* outer) {
    CInstance* inst = wf->inst;
    CFunc* fn = inst->fn;
    const char* file = fn->sig && fn->sig->file ? fn->sig->file : m->path;
    const int line = fn->sig ? fn->sig->line : 0;
    SType* gt = inst->ret;
    if (!is_generator_type(gt) || gt->nargs < 1) {
        wfail(line, file, "generator '%s' without the type of its values", fn->name->chars);
    }

    WFunc* step = wfunc_add(func_type("i", "i"));
    step->nested = true; // compiled here, with its generator function
    WCtx w = {0};
    w.inst = inst;
    w.wf = step;
    w.module = m;
    w.b = &step->body;
    w.file = file;
    w.outer = outer;
    w.kind = WK_FUNCTION;
    w.result_vt = T_I32;
    w.nparams = 1;
    w.gen = true;
    w.gen_elem = gt->args[0];
    names_used_nested(&w.boxnames, fn->body, false);
    w.closure_local = new_local(&w, T_I32);
    w.gen_resume = new_local(&w, T_I32);
    int* plocal = pp_calloc((size_t)inst->nparams + 1, sizeof(int));
    for (int i = 0; i < inst->nparams; ++i) {
        const uint8_t vt = valtype(inst->params[i], line, file);
        plocal[i] = new_local(&w, vt ? vt : T_I32);
        GROW(w.locals, w.nlocals, w.caplocals);
        WLocal* l = &w.locals[w.nlocals++];
        memset(l, 0, sizeof *l);
        l->name = fn->sig->params[i].name ? fn->sig->params[i].name : str_cintern(G->vm, "\x01p");
        l->index = plocal[i];
        l->vt = vt;
        l->type = inst->params[i];
    }

    pauses_walk(fn->body, pause_collect, &w);
    // block $pause { body; done: return 0 } the locals into the frame; return 1
    open_block(&w, W_BLOCK, 0);
    w.gen_exit = w.labels;
    // the first step: a parameter a nested function uses moves into a cell
    gen_running(&w);
    open_block(&w, W_IF, 0);
    for (int i = 0; i < inst->nparams; ++i) {
        ObjString* pname = fn->sig->params[i].name;
        if (pname && names_has(&w.boxnames, pname) && valtype(inst->params[i], line, file)) {
            op_u(&w, W_LOCAL_GET, (uint64_t)plocal[i]);
            declare(&w, pname, inst->params[i], line);
        }
    }

    close_block(&w);
    block_body(&w, fn->body, false, NULL);
    gen_set_state(&w, GEN_DONE_STATE);
    i32c(&w, 0);
    op(&w, W_RETURN);
    close_block(&w);
    const int nlocals = w.nlocal_types;
    for (int l = 1; l <= nlocals; ++l) {
        op_u(&w, W_LOCAL_GET, 0);
        op_u(&w, W_LOCAL_GET, (uint64_t)l);
        switch (w.local_types[l - 1]) {
            case T_I64: mem(&w, W_I64_STORE, 3, frame_slot(l)); break;
            case T_F64: mem(&w, W_F64_STORE, 3, frame_slot(l)); break;
            default: mem(&w, W_I32_STORE, 2, frame_slot(l)); break;
        }
    }

    i32c(&w, 1);
    // in front: the locals from the frame, then the state: done gives nothing, running is an error
    Bytes body = step->body;
    memset(&step->body, 0, sizeof step->body);
    for (int l = 1; l <= nlocals; ++l) {
        op_u(&w, W_LOCAL_GET, 0);
        switch (w.local_types[l - 1]) {
            case T_I64: mem(&w, W_I64_LOAD, 3, frame_slot(l)); break;
            case T_F64: mem(&w, W_F64_LOAD, 3, frame_slot(l)); break;
            default: mem(&w, W_I32_LOAD, 2, frame_slot(l)); break;
        }

        op_u(&w, W_LOCAL_SET, (uint64_t)l);
    }

    op_u(&w, W_LOCAL_GET, 0);
    mem(&w, W_I32_LOAD, 2, 4);
    op_u(&w, W_LOCAL_TEE, (uint64_t)w.gen_resume);
    i32c(&w, GEN_DONE_STATE);
    op(&w, W_I32_EQ);
    open_block(&w, W_IF, 0);
    i32c(&w, 0);
    op(&w, W_RETURN);
    close_block(&w);
    op_u(&w, W_LOCAL_GET, (uint64_t)w.gen_resume);
    i32c(&w, GEN_RUNNING_STATE);
    op(&w, W_I32_EQ);
    open_block(&w, W_IF, 0);
    {
        StrBuf sb;
        sb_init(&sb);
        sb_printf(&sb, "generator '%s' is already running (next() called from its own body)", fn->name->chars);
        i32c(&w, str_addr(str_cintern(G->vm, sb.data)));
        sb_free(&sb);
    }

    host(&w, H_FAIL);
    op(&w, W_UNREACHABLE);
    close_block(&w);
    gen_set_state(&w, GEN_RUNNING_STATE);
    put_all(&step->body, body.data, body.len);
    free(body.data);
    free(w.pauses);
    finish_code(&w, step);
    wf->ncaps = step->ncaps;
    wf->caps = step->caps;

    // the generator function: [step][state 0][value][frame], the closure and the arguments in the frame
    Bytes code = {0};
    uleb(&code, 1);
    uleb(&code, 1);
    put(&code, T_I32);
    WCtx c = {0};
    c.b = &code;
    c.file = file;
    const int p = inst->nparams + 1;
    alloc(&c, 16 + 8 * nlocals);
    op_u(&c, W_LOCAL_SET, (uint64_t)p);
    op_u(&c, W_LOCAL_GET, (uint64_t)p);
    i32c(&c, table_slot(step));
    mem(&c, W_I32_STORE, 2, 0);
    op_u(&c, W_LOCAL_GET, (uint64_t)p);
    op_u(&c, W_LOCAL_GET, 0);
    mem(&c, W_I32_STORE, 2, frame_slot(w.closure_local));
    for (int i = 0; i < inst->nparams; ++i) {
        op_u(&c, W_LOCAL_GET, (uint64_t)p);
        op_u(&c, W_LOCAL_GET, (uint64_t)(i + 1));
        store_slot(&c, inst->params[i], frame_slot(plocal[i]), line);
    }

    op_u(&c, W_LOCAL_GET, (uint64_t)p);
    put(&code, W_END);
    free(plocal);
    free(wf->body.data);
    wf->body = code;
    wf->done = true;
}

static void compile_body(WFunc* wf, CModule* m, Node* program, WCtx* outer) {
    WCtx w = {0};
    w.inst = wf->inst;
    w.wf = wf;
    w.module = m;
    w.b = &wf->body;
    w.file = m->path;
    w.outer = outer;
    w.kind = WK_FUNCTION;
    CFunc* fn = wf->inst->fn;
    if (wf->inst->dyn) {
        wfail(wf->inst->dyn_line, w.file, "'%s' is not statically typed (%s)", fn && fn->name ? fn->name->chars : "the top level",
              wf->inst->dyn_reason ? wf->inst->dyn_reason : "dynamic");
    }

    if (fn && fn->sig && fn->sig->generator) {
        compile_generator(wf, m, outer);

        return;
    }

    if (fn) {
        w.file = fn->sig && fn->sig->file ? fn->sig->file : w.file;
        w.result_vt = valtype(wf->inst->ret, fn->sig ? fn->sig->line : 0, w.file);
        names_used_nested(&w.boxnames, fn->body, false);
        // parameter 0 is the function value; a parameter a nested function uses moves into a cell
        w.nparams = wf->inst->nparams + 1;
        for (int i = 0; i < wf->inst->nparams; ++i) {
            ObjString* pname = fn->sig->params[i].name ? fn->sig->params[i].name : str_cintern(G->vm, "\x01p");
            GROW(w.locals, w.nlocals, w.caplocals);
            WLocal* l = &w.locals[w.nlocals++];
            memset(l, 0, sizeof *l);
            l->name = pname;
            l->index = i + 1;
            l->vt = valtype(wf->inst->params[i], fn->sig->line, w.file);
            l->type = wf->inst->params[i];
        }

        for (int i = 0; i < wf->inst->nparams; ++i) {
            ObjString* pname = fn->sig->params[i].name;
            if (pname && names_has(&w.boxnames, pname) && w.locals[i].vt) {
                op_u(&w, W_LOCAL_GET, (uint64_t)(i + 1));
                declare(&w, pname, wf->inst->params[i], fn->sig->line);
            }
        }

        // an expression-bodied lambda gives its expression; a block body returns with `return`
        const int nbody = fn->body->as.block.stmts.count;
        Node* tail = nbody ? fn->body->as.block.stmts.items[nbody - 1] : NULL;
        const bool implicit = fn->sig && fn->sig->implicit_result && tail && tail->kind == S_EXPR && tail->as.expr.yield;
        block_body(&w, fn->body, implicit, wf->inst->ret);
        if (!implicit && valtype(wf->inst->ret, fn->sig->line, w.file)) {
            // a function that ends without a return: the checker allows it only when every path returns
            op(&w, W_UNREACHABLE);
        }
    } else {
        w.top = true;
        names_used_nested(&w.boxnames, program, false);
        for (int i = 0; i < program->as.block.stmts.count; ++i) {
            wstmt(&w, program->as.block.stmts.items[i]);
        }
    }

    finish_code(&w, wf);
}

// a module's top level as a function that runs once (the main module's is exported as main)
static int module_init(CModule* m) {
    for (int i = 0; i < G->nmodules; ++i) {
        if (G->modules[i].module == m) {
            return G->modules[i].func;
        }
    }

    GROW(G->modules, G->nmodules, G->capmodules);
    WModule* wm = &G->modules[G->nmodules++];
    wm->module = m;
    WFunc* f = wfunc_for(m->top);
    wm->func = f->index;
    if (m->program) {
        // the flag that it ran: a guard around the body
        const WGlobal* flag = new_global(m, str_cintern(G->vm, "\x01ran"), T_I32);
        wm->flag = flag->index;
        compile_body(f, m, m->program, NULL);
        // wrap: if ran { return } ran = 1; ... (the body's locals header stays first)
        Bytes guarded = {0};
        size_t pos = 0;
        uint64_t nl = 0;
        int shift = 0;
        uint8_t x;
        do {
            x = f->body.data[pos++];
            nl |= (uint64_t)(x & 0x7F) << shift;
            shift += 7;
        } while (x & 0x80);
        for (uint64_t i = 0; i < nl; ++i) {
            do {
                x = f->body.data[pos++];
            } while (x & 0x80);
            pos++; // the type
        }

        put_all(&guarded, f->body.data, pos);
        put(&guarded, W_GLOBAL_GET);
        uleb(&guarded, (uint64_t)flag->index);
        put(&guarded, W_IF);
        put(&guarded, T_NONE);
        put(&guarded, W_RETURN);
        put(&guarded, W_END);
        put(&guarded, W_I32_CONST);
        sleb(&guarded, 1);
        put(&guarded, W_GLOBAL_SET);
        uleb(&guarded, (uint64_t)flag->index);
        put_all(&guarded, f->body.data + pos, f->body.len - pos);
        free(f->body.data);
        f->body = guarded;
    } else {
        Bytes empty = {0};
        uleb(&empty, 0);
        put(&empty, W_END);
        f->body = empty;
        f->done = true;
    }

    return wm->func;
}



// ---------------------------------------------------------------- the files

// alloc(size) -> address, 8-byte aligned, zeroed: the host's heap (gcAlloc in JS_HOST), which a
// conservative mark & sweep collects while no wasm code runs; exported for the host
static void alloc_body(Bytes* b) {
    static const uint8_t code[] = {
        0,                                // no locals
        W_LOCAL_GET, 0, W_CALL, H_ALLOC, // gc_alloc(size)
        W_END,
    };
    put_all(b, code, sizeof code);
}

static const char* JS_HOST =
    "// Generated by pp --target=wasm: runs %s.wasm in node (node %s.mjs) or in a browser (%s.html).\n"
    "const T = [];\n"
    "%s"
    "const NAMES = [%s];\n"
    "const SIGS = [];\n"
    "%s"
    "const TID = {int: %d, float: %d, string: %d, bool: %d, box: %d};\n"
    "const HEAP_START = %d;\n"
    "const isNode = typeof process !== 'undefined' && !!(process.versions && process.versions.node);\n"
    "let inst = null;\n"
    "let line = '';\n"
    "let write = (s) => { if (isNode) { process.stdout.write(s); } else { const o = document.getElementById('output'); "
    "if (o) { o.textContent += s; } else { console.log(s); } } };\n"
    "const enc = new TextEncoder();\n"
    "const dec = new TextDecoder();\n"
    "const view = () => new DataView(inst.exports.memory.buffer);\n"
    "const bytes = () => new Uint8Array(inst.exports.memory.buffer);\n"
    "class PpError extends Error {}\n"
    "function fail(message) { write(line); line = ''; throw new PpError(message); }\n"
    "function rawStr(p) { const n = view().getUint32(p, true); return bytes().slice(p + 4, p + 4 + n); }\n"
    "function readStr(p) { return dec.decode(rawStr(p)); }\n"
    "function newRaw(b) { const p = inst.exports.alloc(4 + b.length); view().setUint32(p, b.length, true); bytes().set(b, p + 4); return p; }\n"
    "function newStr(s) { return newRaw(enc.encode(s)); }\n"
    "function slot(a, t) { const v = view(); switch (T[t].k) { case 'int': return v.getBigInt64(a, true); "
    "case 'float': return v.getFloat64(a, true); default: return v.getInt32(a, true); } }\n"
    "function gfmt(x, p) {\n"
    "  if (x === 0) { return Object.is(x, -0) ? '-0' : '0'; }\n"
    "  const s = x.toExponential(p - 1);\n"
    "  const e = parseInt(s.slice(s.indexOf('e') + 1), 10);\n"
    "  if (e < -4 || e >= p) {\n"
    "    let m = s.slice(0, s.indexOf('e'));\n"
    "    if (m.includes('.')) { m = m.replace(/0+$/, '').replace(/\\.$/, ''); }\n"
    "    const a = Math.abs(e);\n"
    "    return m + 'e' + (e < 0 ? '-' : '+') + (a < 10 ? '0' + a : String(a));\n"
    "  }\n"
    "  let f = x.toFixed(Math.max(0, p - 1 - e));\n"
    "  if (f.includes('.')) { f = f.replace(/0+$/, '').replace(/\\.$/, ''); }\n"
    "  return f;\n"
    "}\n"
    "function fmtFloat(x) {\n"
    "  if (Number.isNaN(x)) { return 'nan'; }\n"
    "  if (!Number.isFinite(x)) { return x > 0 ? 'inf' : '-inf'; }\n"
    "  let s = '';\n"
    "  for (let p = 15; p <= 17; ++p) { s = gfmt(x, p); if (parseFloat(s) === x) { break; } }\n"
    "  return /[.eE]/.test(s) ? s : s + '.0';\n"
    "}\n"
    "function quote(s) {\n"
    "  let r = '\"';\n"
    "  for (const ch of s) {\n"
    "    const c = ch.codePointAt(0);\n"
    "    if (ch === '\"') { r += '\\\\\"'; } else if (ch === '\\\\') { r += '\\\\\\\\'; } else if (ch === '\\n') { r += '\\\\n'; }\n"
    "    else if (ch === '\\t') { r += '\\\\t'; } else if (ch === '\\r') { r += '\\\\r'; }\n"
    "    else if (c < 0x20) { r += '\\\\u' + c.toString(16).padStart(4, '0'); } else { r += ch; }\n"
    "  }\n"
    "  return r + '\"';\n"
    "}\n"
    "const pad = (n) => '    '.repeat(n);\n"
    "function complex(t, x) {\n"
    "  const d = T[t];\n"
    "  if (d.k === 'box') { const id = view().getInt32(x, true); return complex(id, slot(x + 8, id)); }\n"
    "  if (d.k === 'record') { return view().getInt32(view().getInt32(x, true), true) > 0; }\n"
    "  if (d.k === 'array') { const v = view(); const n = v.getInt32(x, true); const data = v.getInt32(x + 8, true);\n"
    "    for (let i = 0; i < n; ++i) { if (complex(d.e, slot(data + 8 * i, d.e))) { return true; } } }\n"
    "  return false;\n"
    "}\n"
    "function fmt(t, x, top, ind) {\n"
    "  const d = T[t];\n"
    "  const v = view();\n"
    "  switch (d.k) {\n"
    "    case 'int': return x.toString();\n"
    "    case 'float': return fmtFloat(x);\n"
    "    case 'bool': return x ? 'true' : 'false';\n"
    "    case 'void': return 'void';\n"
    "    case 'box': { const id = v.getInt32(x, true); return fmt(id, slot(x + 8, id), top, ind); }\n"
    "    case 'function': return '<function ' + FNAMES[v.getInt32(x, true)] + '>';\n"
    "    case 'string': return top ? readStr(x) : quote(readStr(x));\n"
    "    case 'array': {\n"
    "      const n = v.getInt32(x, true);\n"
    "      if (n === 0) { return '[]'; }\n"
    "      const data = v.getInt32(x + 8, true);\n"
    "      const items = [];\n"
    "      for (let i = 0; i < n; ++i) { items.push(slot(data + 8 * i, d.e)); }\n"
    "      if (!items.some((it) => complex(d.e, it))) { return '[' + items.map((it) => fmt(d.e, it, false, ind)).join(', ') + ']'; }\n"
    "      return '[\\n' + items.map((it, i) => pad(ind + 1) + fmt(d.e, it, false, ind + 1) + (i + 1 < n ? ',' : '') + '\\n').join('') + pad(ind) + ']';\n"
    "    }\n"
    "    case 'record': {\n"
    "      const shape = v.getInt32(x, true);\n"
    "      const n = v.getInt32(shape, true);\n"
    "      if (n === 0) { return '{}'; }\n"
    "      let s = '{\\n';\n"
    "      for (let i = 0; i < n; ++i) {\n"
    "        const name = NAMES[v.getInt32(shape + 4 + 8 * i, true)];\n"
    "        const ft = v.getInt32(shape + 8 + 8 * i, true);\n"
    "        s += pad(ind + 1) + quote(name) + ': ' + fmt(ft, slot(x + 8 + 8 * i, ft), false, ind + 1) + (i + 1 < n ? ',' : '') + '\\n';\n"
    "      }\n"
    "      return s + pad(ind) + '}';\n"
    "    }\n"
    "    case 'tuple': return 'tuple(' + d.e.map((et, i) => fmt(et, slot(x + 8 * i, et), false, ind)).join(', ') + ')';\n"
    "    case 'enum': {\n"
    "      const [name, ctor, pays] = d.v[v.getInt32(x, true)];\n"
    "      if (!ctor) { return d.n + '.' + name; }\n"
    "      return name + '(' + pays.map((pt, i) => fmt(pt, slot(x + 8 + 8 * i, pt), false, ind)).join(', ') + ')';\n"
    "    }\n"
    "    case 'map': return mapFmt(x, true, ind);\n"
    "    case 'set': return mapFmt(x, false, ind);\n"
    "    case 'range': {\n"
    "      const a = v.getBigInt64(x, true), b = v.getBigInt64(x + 8, true), st = v.getBigInt64(x + 16, true);\n"
    "      if (st === 1n || st === -1n) { return a + '...' + b; }\n"
    "      return 'range(' + a + ', ' + b + ', ' + st + ')';\n"
    "    }\n"
    "    default: return '<' + (d.n || d.k) + '>';\n"
    "  }\n"
    "}\n"
    "function cmpRaw(a, b) { const n = Math.min(a.length, b.length); for (let i = 0; i < n; ++i) { if (a[i] !== b[i]) { return a[i] < b[i] ? -1 : 1; } }\n"
    "  return a.length === b.length ? 0 : a.length < b.length ? -1 : 1; }\n"
    "const MIN = -9223372036854775808n;\n"
    "// The heap: objects of 8-byte multiples, freed into lists by size. collect() marks conservatively\n"
    "// from the roots (the module's i32 globals and pinned objects): every aligned word of a live object\n"
    "// that is the address of an object keeps it. It runs only while no wasm code is running (depth 0),\n"
    "// so the locals of running functions, which it cannot see, hold nothing.\n"
    "export const heap = { top: HEAP_START, sizes: new Map(), free: new Map(), since: 0, live: 0, depth: 0, pins: new Map() };\n"
    "function gcAlloc(n) {\n"
    "  const size = Math.max(8, (n + 7) & ~7);\n"
    "  let a;\n"
    "  const list = heap.free.get(size);\n"
    "  if (list && list.length) { a = list.pop(); bytes().fill(0, a, a + size); }\n"
    "  else { a = heap.top; heap.top += size; const mem = inst.exports.memory; const need = heap.top - mem.buffer.byteLength;\n"
    "    if (need > 0) { mem.grow(Math.ceil(need / 65536) + 1); } }\n"
    "  heap.sizes.set(a, size);\n"
    "  heap.since += size;\n"
    "  return a;\n"
    "}\n"
    "export function collect() {\n"
    "  const v = view(), marked = new Set(), work = [];\n"
    "  const add = (p) => { if (heap.sizes.has(p) && !marked.has(p)) { marked.add(p); work.push(p); } };\n"
    "  for (const [k, g] of Object.entries(inst.exports)) { if (/^g\\d+$/.test(k)) { add(g.value); } }\n"
    "  for (const p of heap.pins.keys()) { add(p); }\n"
    "  while (work.length) { const p = work.pop(), end = p + heap.sizes.get(p); for (let q = p; q < end; q += 4) { add(v.getInt32(q, true)); } }\n"
    "  let live = 0;\n"
    "  for (const [p, size] of heap.sizes) {\n"
    "    if (marked.has(p)) { live += size; continue; }\n"
    "    heap.sizes.delete(p);\n"
    "    let l = heap.free.get(size); if (!l) { l = []; heap.free.set(size, l); } l.push(p);\n"
    "  }\n"
    "  heap.live = live; heap.since = 0;\n"
    "}\n"
    "function maybeCollect() { if (heap.depth === 0 && heap.since > Math.max(1 << 20, heap.live)) { collect(); } }\n"
    "// what JavaScript keeps (an event handler, a value on the page) stays alive until unpinned\n"
    "export function pin(p) { heap.pins.set(p, (heap.pins.get(p) || 0) + 1); }\n"
    "export function unpin(p) { const n = (heap.pins.get(p) || 1) - 1; if (n) { heap.pins.set(p, n); } else { heap.pins.delete(p); } }\n"
    "// calls into wasm from JavaScript go through here: the collector waits until the outermost one returns\n"
    "export function enter(f) { heap.depth++; try { return f(); } finally { heap.depth--; maybeCollect(); } }\n"
    "const num = (x) => typeof x === 'bigint' ? Number(x) : x;\n"
    "function store(a, t, x) { const v = view(); switch (T[t].k) { case 'int': v.setBigInt64(a, x, true); break;\n"
    "  case 'float': v.setFloat64(a, x, true); break; default: v.setInt32(a, x, true); } }\n"
    "function variantOf(d, name) { return d.v.findIndex((x) => x[0] === name); }\n"
    "function toJs(t, x) {\n"
    "  const d = T[t];\n"
    "  switch (d.k) {\n"
    "    case 'bool': return x !== 0;\n"
    "    case 'string': return readStr(x);\n"
    "    case 'array': { const v = view(); const n = v.getInt32(x, true), data = v.getInt32(x + 8, true); const out = [];\n"
    "      for (let i = 0; i < n; ++i) { out.push(toJs(d.e, slot(data + 8 * i, d.e))); } return out; }\n"
    "    case 'tuple': return d.e.map((et, i) => toJs(et, slot(x + 8 * i, et)));\n"
    "    case 'box': { const id = view().getInt32(x, true); return toJs(id, slot(x + 8, id)); }\n"
    "    case 'function': { const f = (...a) => callFn(d, x, a); f.addr = x; return f; }\n"
    "    case 'enum': if (d.n === 'Option') { const v = view(); if (d.v[v.getInt32(x, true)][0] === 'none') { return null; }\n"
    "      return toJs(d.v[variantOf(d, 'some')][2][0], slot(x + 8, d.v[variantOf(d, 'some')][2][0])); } return x;\n"
    "    default: return x;\n"
    "  }\n"
    "}\n"
    "function fromJs(t, x) {\n"
    "  const d = T[t];\n"
    "  switch (d.k) {\n"
    "    case 'int': return typeof x === 'bigint' ? x : BigInt(Math.trunc(x));\n"
    "    case 'float': return Number(x);\n"
    "    case 'bool': return x ? 1 : 0;\n"
    "    case 'function': return typeof x === 'function' ? x.addr : x;\n"
    "    case 'string': return newStr(String(x));\n"
    "    case 'array': { const n = x.length; const a = inst.exports.alloc(16), data = inst.exports.alloc(8 * Math.max(n, 1));\n"
    "      const v = view(); v.setInt32(a, n, true); v.setInt32(a + 4, Math.max(n, 1), true); v.setInt32(a + 8, data, true);\n"
    "      x.forEach((it, i) => store(data + 8 * i, d.e, fromJs(d.e, it))); return a; }\n"
    "    case 'tuple': { const a = inst.exports.alloc(8 * d.e.length); d.e.forEach((et, i) => store(a + 8 * i, et, fromJs(et, x[i]))); return a; }\n"
    "    case 'box': { const id = typeof x === 'bigint' ? TID.int : typeof x === 'number' ? TID.float : typeof x === 'boolean' ? TID.bool : TID.string;\n"
    "      const b = inst.exports.alloc(16); view().setInt32(b, id, true); store(b + 8, id, fromJs(id, x)); return b; }\n"
    "    case 'enum': if (d.n === 'Option') { const some = x !== null && x !== undefined; const k = variantOf(d, some ? 'some' : 'none');\n"
    "      const e = inst.exports.alloc(some ? 16 : 8); const v = view(); v.setInt32(e, k, true); v.setInt32(e + 4, some ? 1 : 0, true);\n"
    "      if (some) { const pt = d.v[k][2][0]; store(e + 8, pt, fromJs(pt, x)); } return e; } return x;\n"
    "    default: return x;\n"
    "  }\n"
    "}\n"
    "function callFn(d, x, a) {\n"
    "  const f = inst.exports.table.get(view().getInt32(x, true));\n"
    "  const r = f(x, ...a.map((v, i) => fromJs(d.p[i], v)));\n"
    "  return d.r < 0 ? undefined : toJs(d.r, r);\n"
    "}\n"
    "function eqRaw(t, a, b) {\n"
    "  const d = T[t], v = view();\n"
    "  switch (d.k) {\n"
    "    case 'string': return cmpRaw(rawStr(a), rawStr(b)) === 0;\n"
    "    case 'array': { const n = v.getInt32(a, true); if (n !== v.getInt32(b, true)) { return false; }\n"
    "      const da = v.getInt32(a + 8, true), db = v.getInt32(b + 8, true);\n"
    "      for (let i = 0; i < n; ++i) { if (!eqRaw(d.e, slot(da + 8 * i, d.e), slot(db + 8 * i, d.e))) { return false; } } return true; }\n"
    "    case 'tuple': return d.e.every((et, i) => eqRaw(et, slot(a + 8 * i, et), slot(b + 8 * i, et)));\n"
    "    case 'enum': { const k = v.getInt32(a, true); if (k !== v.getInt32(b, true)) { return false; }\n"
    "      return d.v[k][2].every((pt, i) => eqRaw(pt, slot(a + 8 + 8 * i, pt), slot(b + 8 + 8 * i, pt))); }\n"
    "    case 'record': { const sa = v.getInt32(a, true), sb = v.getInt32(b, true); const n = v.getInt32(sa, true);\n"
    "      if (n !== v.getInt32(sb, true)) { return false; }\n"
    "      for (let i = 0; i < n; ++i) { const ft = v.getInt32(sa + 8 + 8 * i, true); if (!eqRaw(ft, slot(a + 8 + 8 * i, ft), slot(b + 8 + 8 * i, ft))) { return false; } }\n"
    "      return true; }\n"
    "    case 'box': { const ia = v.getInt32(a, true), ib = v.getInt32(b, true); return ia === ib && eqRaw(ia, slot(a + 8, ia), slot(b + 8, ib)); }\n"
    "    case 'map': case 'set': return mapEq(a, b);\n"
    "    default: return a === b;\n"
    "  }\n"
    "}\n"
    "function runtimeName(t, x) {\n"
    "  const d = T[t];\n"
    "  switch (d.k) {\n"
    "    case 'box': { const id = view().getInt32(x, true); return runtimeName(id, slot(x + 8, id)); }\n"
    "    case 'record': return d.n || 'object';\n"
    "    case 'enum': return d.n;\n"
    "    case 'map': return 'Map';\n"
    "    case 'set': return 'Set';\n"
    "    default: return d.k;\n"
    "  }\n"
    "}\n"
    "const jsCmp = (a, b) => a < b ? -1 : a > b ? 1 : 0;\n"
    "const arith = (a, b, f) => typeof a === 'bigint' && typeof b === 'bigint' ? BigInt.asIntN(64, f(a, b)) : f(num(a), num(b));\n"
    "const fmtJs = (x) => typeof x === 'number' ? fmtFloat(x) : String(x);\n"
    "function rawArr(a) { const v = view(); return { n: v.getInt32(a, true), cap: v.getInt32(a + 4, true), data: v.getInt32(a + 8, true) }; }\n"
    "function cfmt(flags, width, prec, c, a) {\n"
    "  const x = a.v;\n"
    "  let s;\n"
    "  switch (c) {\n"
    "    case 'd': case 'i': s = BigInt(x).toString(); break;\n"
    "    case 'u': s = BigInt.asUintN(64, BigInt(x)).toString(); break;\n"
    "    case 'f': case 'F': s = num(x).toFixed(prec === undefined ? 6 : +prec); break;\n"
    "    case 'e': case 'E': s = num(x).toExponential(prec === undefined ? 6 : +prec).replace(/e([+-])(\\d)$/, 'e$10$2'); break;\n"
    "    case 'g': case 'G': s = gfmt(num(x), prec === undefined ? 6 : Math.max(1, +prec)); break;\n"
    "    case 'x': case 'X': s = (flags.includes('#') ? '0x' : '') + BigInt.asUintN(64, BigInt(x)).toString(16); break;\n"
    "    case 'o': s = (flags.includes('#') ? '0' : '') + BigInt.asUintN(64, BigInt(x)).toString(8); break;\n"
    "    case 'b': s = BigInt.asUintN(64, BigInt(x)).toString(2); break;\n"
    "    case 'c': s = typeof x === 'string' ? x : String.fromCodePoint(num(x)); break;\n"
    "    default: s = typeof x === 'string' ? x : fmt(a.t, a.r, true, 0); if (prec !== undefined) { s = [...s].slice(0, +prec).join(''); }\n"
    "  }\n"
    "  if (c === 'X' || c === 'E' || c === 'G') { s = s.toUpperCase(); }\n"
    "  if (/[difeEgG]/.test(c) && !s.startsWith('-')) { if (flags.includes('+')) { s = '+' + s; } else if (flags.includes(' ')) { s = ' ' + s; } }\n"
    "  const w = +width || 0, n = [...s].length;\n"
    "  if (n < w) { s = flags.includes('-') ? s + ' '.repeat(w - n) : flags.includes('0') && c !== 's' && c !== 'c' ?\n"
    "    (s[0] === '-' || s[0] === '+' || s[0] === ' ' ? s[0] + s.slice(1).padStart(w - 1, '0') : s.padStart(w, '0')) : ' '.repeat(w - n) + s; }\n"
    "  return s;\n"
    "}\n"
    "// ---- Map / Set: objects of the heap (the collector sees through them), so wasm code can\n"
    "// hold them like any value. [n][cap][keys][vals][index][icap][kt][vt]: n entries in insertion\n"
    "// order, their keys and values in 8-byte slots of the types kt / vt (a set: vt -2, no values; -1:\n"
    "// not known yet, taken from the first call that says), an open-addressing index of entry + 1.\n"
    "// Keys are hashed and compared here; the natives convert between the stored types and the types of\n"
    "// the call that uses them.\n"
    "class Raw { constructor(v) { this.v = v; } }\n"
    "function mapNew(kt, vt) { const m = inst.exports.alloc(32); const v = view(); v.setInt32(m + 24, kt, true); v.setInt32(m + 28, vt, true); return m; }\n"
    "function mh(m) { const v = view(); return { n: v.getInt32(m, true), cap: v.getInt32(m + 4, true), keys: v.getInt32(m + 8, true),\n"
    "  vals: v.getInt32(m + 12, true), index: v.getInt32(m + 16, true), icap: v.getInt32(m + 20, true), kt: v.getInt32(m + 24, true),\n"
    "  vt: v.getInt32(m + 28, true) }; }\n"
    "const isColl = (t) => t >= 0 && (T[t].k === 'map' || T[t].k === 'set');\n"
    "function keyError(t, x) {\n"
    "  const name = runtimeName(t, x), k = T[t].k;\n"
    "  const why = k === 'array' || k === 'record' || k === 'map' || k === 'set' ? ' (it can change after it is stored; use a tuple instead)' : '';\n"
    "  fail(`${/^[aeiou]/i.test(name) ? 'an' : 'a'} ${name} cannot be a map key or set element${why}`);\n"
    "}\n"
    "const mix = (h, x) => (Math.imul(h ^ x, 16777619) >>> 0);\n"
    "function hashRaw(t, x) {\n"
    "  const d = T[t], v = view();\n"
    "  switch (d.k) {\n"
    "    case 'int': return Number(BigInt.asUintN(32, x ^ (x >> 32n)));\n"
    "    case 'float': { if (Number.isInteger(x) && Math.abs(x) < 2 ** 53) { return hashRaw(TID.int, BigInt(x)); }\n"
    "      const b = new DataView(new ArrayBuffer(8)); b.setFloat64(0, x); return (b.getUint32(0) ^ b.getUint32(4)) >>> 0; }\n"
    "    case 'bool': return x;\n"
    "    case 'string': { let h = 2166136261; const n = v.getUint32(x, true), bs = bytes(); for (let i = 0; i < n; ++i) { h = mix(h, bs[x + 4 + i]); } return h; }\n"
    "    case 'tuple': return d.e.reduce((h, et, i) => mix(h, hashRaw(et, slot(x + 8 * i, et))), 7);\n"
    "    case 'enum': { const k = v.getInt32(x, true); return d.v[k][2].reduce((h, pt, i) => mix(h, hashRaw(pt, slot(x + 8 + 8 * i, pt))), k + 1); }\n"
    "    case 'range': return mix(mix(hashRaw(TID.int, v.getBigInt64(x, true)), hashRaw(TID.int, v.getBigInt64(x + 8, true))), hashRaw(TID.int, v.getBigInt64(x + 16, true)));\n"
    "    case 'box': { const id = v.getInt32(x, true); return hashRaw(id, slot(x + 8, id)); }\n"
    "    default: keyError(t, x);\n"
    "  }\n"
    "}\n"
    "// a raw value of type `from` as one of type `to` (unboxed, boxed, int to float, or rebuilt)\n"
    "function conv(from, to, x) {\n"
    "  if (from === to || from < 0 || to < 0) { return x; }\n"
    "  const f = T[from], t = T[to];\n"
    "  if (f.k === 'box' && t.k !== 'box') { const id = view().getInt32(x, true); return conv(id, to, slot(x + 8, id)); }\n"
    "  if (t.k === 'box') { if (f.k === 'box') { return x; } const b = inst.exports.alloc(16); view().setInt32(b, from, true); store(b + 8, from, x); return b; }\n"
    "  if (f.k === 'int' && t.k === 'float') { return Number(x); }\n"
    "  if (f.k === t.k && f.k !== 'array' && f.k !== 'tuple') { return x; }\n"
    "  return fromJs(to, toJs(from, x));\n"
    "}\n"
    "function mapFind(m, x) {\n"
    "  const h = mh(m);\n"
    "  if (!h.icap) { return -1; }\n"
    "  const v = view();\n"
    "  for (let i = hashRaw(h.kt, x) & (h.icap - 1); ; i = (i + 1) & (h.icap - 1)) {\n"
    "    const e = v.getInt32(h.index + 4 * i, true);\n"
    "    if (e === 0) { return -1; }\n"
    "    if (eqRaw(h.kt, slot(h.keys + 8 * (e - 1), h.kt), x)) { return e - 1; }\n"
    "  }\n"
    "}\n"
    "// the index again, with room for n entries\n"
    "function mapReindex(m, n) {\n"
    "  let h = mh(m);\n"
    "  if (h.icap < 2 * n || !h.icap) { let c = 8; while (c < 2 * n) { c *= 2; } const ix = inst.exports.alloc(4 * c); const v = view();\n"
    "    v.setInt32(m + 16, ix, true); v.setInt32(m + 20, c, true); h = mh(m); }\n"
    "  else { bytes().fill(0, h.index, h.index + 4 * h.icap); }\n"
    "  const v = view();\n"
    "  for (let k = 0; k < h.n; ++k) {\n"
    "    let i = hashRaw(h.kt, slot(h.keys + 8 * k, h.kt)) & (h.icap - 1);\n"
    "    while (v.getInt32(h.index + 4 * i, true)) { i = (i + 1) & (h.icap - 1); }\n"
    "    v.setInt32(h.index + 4 * i, k + 1, true);\n"
    "  }\n"
    "}\n"
    "// key and value raw in the map's types (value undefined for a set): true when the key is new\n"
    "function mapPut(m, k, x) {\n"
    "  const at = mapFind(m, k);\n"
    "  let h = mh(m);\n"
    "  if (at >= 0) { if (h.vt >= 0) { store(h.vals + 8 * at, h.vt, x); } return false; }\n"
    "  if (h.n === h.cap) {\n"
    "    const nc = h.cap ? h.cap * 2 : 8, nk = inst.exports.alloc(8 * nc), nv = h.vt === -2 ? 0 : inst.exports.alloc(8 * nc);\n"
    "    if (h.n) { bytes().copyWithin(nk, h.keys, h.keys + 8 * h.n); if (nv) { bytes().copyWithin(nv, h.vals, h.vals + 8 * h.n); } }\n"
    "    const v = view(); v.setInt32(m + 4, nc, true); v.setInt32(m + 8, nk, true); v.setInt32(m + 12, nv, true); h = mh(m);\n"
    "  }\n"
    "  store(h.keys + 8 * h.n, h.kt, k);\n"
    "  if (h.vt >= 0) { store(h.vals + 8 * h.n, h.vt, x); }\n"
    "  view().setInt32(m, h.n + 1, true);\n"
    "  if (2 * (h.n + 1) > h.icap) { mapReindex(m, h.n + 1); return true; }\n"
    "  const v = view();\n"
    "  let i = hashRaw(h.kt, k) & (h.icap - 1);\n"
    "  while (v.getInt32(h.index + 4 * i, true)) { i = (i + 1) & (h.icap - 1); }\n"
    "  v.setInt32(h.index + 4 * i, h.n + 1, true);\n"
    "  return true;\n"
    "}\n"
    "function mapRemove(m, k) {\n"
    "  const at = mapFind(m, k);\n"
    "  if (at < 0) { return null; }\n"
    "  const h = mh(m), old = h.vt >= 0 ? slot(h.vals + 8 * at, h.vt) : 0;\n"
    "  bytes().copyWithin(h.keys + 8 * at, h.keys + 8 * (at + 1), h.keys + 8 * h.n);\n"
    "  if (h.vt >= 0) { bytes().copyWithin(h.vals + 8 * at, h.vals + 8 * (at + 1), h.vals + 8 * h.n); }\n"
    "  view().setInt32(m, h.n - 1, true);\n"
    "  mapReindex(m, h.n - 1);\n"
    "  return { v: old };\n"
    "}\n"
    "// the stored types, taken from a call's types while they are not known\n"
    "function adopt(m, kt, vt) {\n"
    "  const v = view();\n"
    "  if (v.getInt32(m + 24, true) < 0 && kt >= 0) { v.setInt32(m + 24, kt, true); }\n"
    "  if (v.getInt32(m + 28, true) === -1 && vt >= 0) { v.setInt32(m + 28, vt, true); }\n"
    "}\n"
    "function mapArg(self, i) { const m = self.raw[i], d = T[self.ts[i]]; if (d.k === 'map' || d.k === 'set') { adopt(m, d.kt, d.vt); } return m; }\n"
    "function keyIn(self, m, i) { adopt(m, self.ts[i], -1); return conv(self.ts[i], mh(m).kt, self.raw[i]); }\n"
    "function valIn(self, m, i) { adopt(m, -1, self.ts[i]); return conv(self.ts[i], mh(m).vt, self.raw[i]); }\n"
    "// putting a key (and value) of the types kt / vt\n"
    "function putTyped(m, kt, k, vt, x) { adopt(m, kt, vt); const h = mh(m); return mapPut(m, conv(kt, h.kt, k), h.vt === -2 ? undefined : conv(vt, h.vt, x)); }\n"
    "function optRaw(t, has, x, xt) {\n"
    "  const d = T[t], k = variantOf(d, has ? 'some' : 'none'), e = inst.exports.alloc(has ? 16 : 8);\n"
    "  if (has) { const pt = d.v[k][2][0]; x = conv(xt, pt, x); store(e + 8, pt, x); }\n"
    "  const v = view(); v.setInt32(e, k, true); v.setInt32(e + 4, has ? 1 : 0, true);\n"
    "  return new Raw(e);\n"
    "}\n"
    "// an array of type t from raw values of type xt (each made by f(i))\n"
    "function arrRaw(t, n, xt, f) {\n"
    "  const et = T[t].e, a = inst.exports.alloc(16), data = inst.exports.alloc(8 * Math.max(n, 1));\n"
    "  const v = view(); v.setInt32(a, n, true); v.setInt32(a + 4, Math.max(n, 1), true); v.setInt32(a + 8, data, true);\n"
    "  for (let i = 0; i < n; ++i) { store(data + 8 * i, et, conv(xt, et, f(i))); }\n"
    "  return new Raw(a);\n"
    "}\n"
    "// map(from) / set(from): from an array, a map or set, a range, a record\n"
    "function mapFrom(self, isSet) {\n"
    "  const d = self.rt >= 0 && isColl(self.rt) ? T[self.rt] : { kt: -1, vt: -1 };\n"
    "  const m = mapNew(d.kt, isSet ? -2 : d.vt);\n"
    "  if (!self.ts.length) { return m; }\n"
    "  const t = self.ts[0], s = T[t], x = self.raw[0];\n"
    "  const add = (kt, k, vt, val) => putTyped(m, kt, k, vt, val);\n"
    "  switch (s.k) {\n"
    "    case 'array': { const { n, data } = rawArr(x);\n"
    "      for (let i = 0; i < n; ++i) { const it = slot(data + 8 * i, s.e);\n"
    "        if (isSet) { add(s.e, it, -1, 0); }\n"
    "        else { const tt = T[s.e]; if (tt.k !== 'tuple' || tt.e.length !== 2) { fail('map(...) takes an array of tuple(key, value) pairs'); }\n"
    "          add(tt.e[0], slot(it, tt.e[0]), tt.e[1], slot(it + 8, tt.e[1])); } }\n"
    "      break; }\n"
    "    case 'map': case 'set': { const h = mh(x); for (let i = 0; i < h.n; ++i) { const k = slot(h.keys + 8 * i, h.kt);\n"
    "        add(h.kt, k, h.vt, h.vt >= 0 ? slot(h.vals + 8 * i, h.vt) : 0); } break; }\n"
    "    case 'range': { const v = view(), a = v.getBigInt64(x, true), b = v.getBigInt64(x + 8, true), st = v.getBigInt64(x + 16, true);\n"
    "      for (let i = a; st > 0n ? i < b : i > b; i += st) { add(TID.int, i, -1, 0); } break; }\n"
    "    case 'record': { const v = view(), shape = v.getInt32(x, true), n = v.getInt32(shape, true);\n"
    "      const fts = []; for (let i = 0; i < n; ++i) { fts.push(view().getInt32(shape + 8 + 8 * i, true)); }\n"
    "      if (fts.some((ft) => ft !== fts[0])) { adopt(m, TID.string, TID.box); }\n"
    "      for (let i = 0; i < n; ++i) { add(TID.string, newStr(NAMES[view().getInt32(shape + 4 + 8 * i, true)]), fts[i], slot(x + 8 + 8 * i, fts[i])); }\n"
    "      break; }\n"
    "    default: fail(`${isSet ? 'set' : 'map'}(...) cannot copy from a ${runtimeName(t, x)}`);\n"
    "  }\n"
    "  return m;\n"
    "}\n"
    "function setCombine(self, mode) {\n"
    "  const a = mapArg(self, 0), b = mapArg(self, 1), ha = mh(a), hb = mh(b);\n"
    "  const d = isColl(self.rt) ? T[self.rt] : { kt: -1 };\n"
    "  const r = mapNew(d.kt >= 0 ? d.kt : ha.kt, -2);\n"
    "  for (let i = 0; i < ha.n; ++i) { const k = slot(ha.keys + 8 * i, ha.kt); const inB = mapFind(b, conv(ha.kt, hb.kt, k)) >= 0;\n"
    "    if (mode === 0 || (mode === 1 && inB) || (mode === 2 && !inB)) { putTyped(r, ha.kt, k, -1, 0); } }\n"
    "  if (mode === 0) { for (let i = 0; i < hb.n; ++i) { putTyped(r, hb.kt, slot(hb.keys + 8 * i, hb.kt), -1, 0); } }\n"
    "  return new Raw(r);\n"
    "}\n"
    "function mapEq(a, b) {\n"
    "  const ha = mh(a), hb = mh(b);\n"
    "  if (ha.n !== hb.n) { return false; }\n"
    "  for (let i = 0; i < ha.n; ++i) { const at = mapFind(b, conv(ha.kt, hb.kt, slot(ha.keys + 8 * i, ha.kt)));\n"
    "    if (at < 0) { return false; }\n"
    "    if (ha.vt >= 0 && !eqRaw(hb.vt, conv(ha.vt, hb.vt, slot(ha.vals + 8 * i, ha.vt)), slot(hb.vals + 8 * at, hb.vt))) { return false; } }\n"
    "  return true;\n"
    "}\n"
    "function mapFmt(x, isMap, ind) {\n"
    "  const h = mh(x), parts = [];\n"
    "  for (let i = 0; i < h.n; ++i) { let s = h.kt >= 0 ? fmt(h.kt, slot(h.keys + 8 * i, h.kt), false, ind) : '?';\n"
    "    if (isMap) { s += ': ' + (h.vt >= 0 ? fmt(h.vt, slot(h.vals + 8 * i, h.vt), false, ind) : '?'); } parts.push(s); }\n"
    "  return (isMap ? 'Map{' : 'Set{') + parts.join(', ') + '}';\n"
    "}\n"
    "function mapCopy(x) { const h = mh(x), r = mapNew(h.kt, h.vt); for (let i = 0; i < h.n; ++i) {\n"
    "  mapPut(r, slot(h.keys + 8 * i, h.kt), h.vt >= 0 ? slot(h.vals + 8 * i, h.vt) : undefined); } return r; }\n"
    "function rangeHas(r, x) {\n"
    "  const v = view(), a = v.getBigInt64(r, true), b = v.getBigInt64(r + 8, true), st = v.getBigInt64(r + 16, true);\n"
    "  if (typeof x !== 'bigint') { if (!Number.isInteger(x)) { return false; } x = BigInt(x); }\n"
    "  if (st > 0n ? x < a || x >= b : x > a || x <= b) { return false; }\n"
    "  return (x - a) %% st === 0n;\n"
    "}\n"
    "const COLLECTIONS = {\n"
    "  std_map_new() { return new Raw(mapFrom(this, false)); },\n"
    "  std_set_new() { return new Raw(mapFrom(this, true)); },\n"
    "  std_map_get() { const m = mapArg(this, 0), at = mapFind(m, keyIn(this, m, 1)), h = mh(m);\n"
    "    return optRaw(this.rt, at >= 0, at >= 0 ? slot(h.vals + 8 * at, h.vt) : 0, h.vt); },\n"
    "  std_map_put() { const m = mapArg(this, 0); const k = keyIn(this, m, 1); mapPut(m, k, valIn(this, m, 2)); },\n"
    "  std_map_has() { const m = mapArg(this, 0); return mapFind(m, keyIn(this, m, 1)) >= 0; },\n"
    "  std_map_remove() { const m = mapArg(this, 0), vt = mh(m).vt, r = mapRemove(m, keyIn(this, m, 1)); return optRaw(this.rt, r !== null, r ? r.v : 0, vt); },\n"
    "  std_set_remove() { const m = mapArg(this, 0); return mapRemove(m, keyIn(this, m, 1)) !== null; },\n"
    "  std_set_add() { const m = mapArg(this, 0); return mapPut(m, keyIn(this, m, 1), undefined); },\n"
    "  std_map_keys() { const m = mapArg(this, 0), h = mh(m); return arrRaw(this.rt, h.n, h.kt, (i) => slot(mh(m).keys + 8 * i, h.kt)); },\n"
    "  std_map_values() { const m = mapArg(this, 0), h = mh(m); return arrRaw(this.rt, h.n, h.vt, (i) => slot(mh(m).vals + 8 * i, h.vt)); },\n"
    "  std_map_entries() { const m = mapArg(this, 0), h = mh(m), tt = T[T[this.rt].e];\n"
    "    return arrRaw(this.rt, h.n, T[this.rt].e, (i) => { const p = inst.exports.alloc(16), c = mh(m);\n"
    "      store(p, tt.e[0], conv(h.kt, tt.e[0], slot(c.keys + 8 * i, h.kt)));\n"
    "      store(p + 8, tt.e[1], conv(h.vt, tt.e[1], slot(mh(m).vals + 8 * i, h.vt))); return p; }); },\n"
    "  std_map_clear() { const m = mapArg(this, 0); view().setInt32(m, 0, true); mapReindex(m, 0); },\n"
    "  std_set_union() { return setCombine(this, 0); },\n"
    "  std_set_intersection() { return setCombine(this, 1); },\n"
    "  std_set_difference() { return setCombine(this, 2); },\n"
    "  std_in_range(x, r) { return rangeHas(this.raw[1], this.raw[0]); },\n"
    "  std_range_has(r, x) { return rangeHas(this.raw[0], this.raw[1]); },\n"
    "};\n"
    "const NATIVES = {\n"
    "  sort(a, cmp) { const c = cmp ? (x, y) => { const r = cmp(x, y); return typeof r === 'boolean' ? (r ? -1 : cmp(y, x) ? 1 : 0) : num(r); } : jsCmp;\n"
    "    return [...a].sort(c); },\n"
    "  map: (a, f) => a.map((x) => f(x)),\n"
    "  filter: (a, f) => a.filter((x) => f(x)),\n"
    "  reduce: (a, f, init) => a.reduce((acc, x) => f(acc, x), init),\n"
    "  join: (a, sep) => a.map(fmtJs).join(sep),\n"
    "  contains(a, x) { if (typeof a === 'string') { return a.includes(x); } const r = this.raw[0], et = T[this.ts[0]].e; const { n, data } = rawArr(r);\n"
    "    for (let i = 0; i < n; ++i) { if (eqRaw(et, slot(data + 8 * i, et), this.raw[1])) { return true; } } return false; },\n"
    "  in(x, a) { return NATIVES.contains.call({ raw: [this.raw[1], this.raw[0]], ts: [this.ts[1], this.ts[0]] }, a, x); },\n"
    "  index_of(a, x) { if (typeof a === 'string') { const i = a.indexOf(x); return i < 0 ? null : BigInt([...a.slice(0, i)].length); }\n"
    "    const r = this.raw[0], et = T[this.ts[0]].e; const { n, data } = rawArr(r);\n"
    "    for (let i = 0; i < n; ++i) { if (eqRaw(et, slot(data + 8 * i, et), this.raw[1])) { return BigInt(i); } } return null; },\n"
    "  reverse: (a) => typeof a === 'string' ? [...a].reverse().join('') : [...a].reverse(),\n"
    "  concat: (a, b) => typeof a === 'string' ? a + b : [...a, ...b],\n"
    "  slice(a, s, e) { const xs = typeof a === 'string' ? [...a] : a; const n = xs.length; let i = num(s), j = e === undefined ? n : num(e);\n"
    "    if (i < 0) { i += n; } if (j < 0) { j += n; } const r = xs.slice(Math.max(0, i), Math.max(0, j)); return typeof a === 'string' ? r.join('') : r; },\n"
    "  copy(a) { return isColl(this.ts[0]) ? new Raw(mapCopy(this.raw[0])) : Array.isArray(a) ? [...a] : a; },\n"
    "  repeat: (a, n) => typeof a === 'string' ? a.repeat(num(n)) : Array.from({ length: num(n) }, () => a).flat(),\n"
    "  multiply: (a, n) => NATIVES.repeat(a, n),\n"
    "  pop() { const a = this.raw[0]; const { n, data } = rawArr(a); if (n === 0) { return null; } view().setInt32(a, n - 1, true);\n"
    "    const et = T[this.ts[0]].e; return toJs(et, slot(data + 8 * (n - 1), et)); },\n"
    "  insert(arr, i) { const a = this.raw[0]; const et = T[this.ts[0]].e; let { n, data } = rawArr(a); let k = num(i); if (k < 0) { k += n + 1; }\n"
    "    if (k < 0 || k > n) { fail(`index ${i} out of range (length ${n})`); } env.arr_push(a); data = rawArr(a).data;\n"
    "    bytes().copyWithin(data + 8 * (k + 1), data + 8 * k, data + 8 * n); store(data + 8 * k, et, this.raw[2]); },\n"
    "  remove(arr, i) { const a = this.raw[0]; const et = T[this.ts[0]].e; const { n, data } = rawArr(a); let k = num(i); if (k < 0) { k += n; }\n"
    "    if (k < 0 || k >= n) { fail(`index ${i} out of range (length ${n})`); } const x = toJs(et, slot(data + 8 * k, et));\n"
    "    bytes().copyWithin(data + 8 * k, data + 8 * (k + 1), data + 8 * n); view().setInt32(a, n - 1, true); return x; },\n"
    "  split: (s, sep) => sep === '' ? [...s] : s.split(sep),\n"
    "  upper: (s) => s.toUpperCase(),\n"
    "  lower: (s) => s.toLowerCase(),\n"
    "  trim: (s) => s.trim(),\n"
    "  replace: (s, a, b) => s.split(a).join(b),\n"
    "  starts_with: (s, p) => s.startsWith(p),\n"
    "  ends_with: (s, p) => s.endsWith(p),\n"
    "  chars: (s) => [...s],\n"
    "  ord: (s) => BigInt(s.codePointAt(0)),\n"
    "  chr: (c) => String.fromCodePoint(num(c)),\n"
    "  parse_int: (s) => /^\\s*[-+]?\\d+\\s*$/.test(s) ? BigInt(s.trim()) : null,\n"
    "  parse_float: (s) => /^\\s*[-+]?(\\d+\\.?\\d*|\\.\\d+)([eE][-+]?\\d+)?\\s*$/.test(s) ? parseFloat(s) : null,\n"
    "  int(s) { const r = NATIVES.parse_int(s); if (r === null) { fail(`cannot convert \"${s}\" to int`); } return r; },\n"
    "  float(s) { const r = NATIVES.parse_float(s); if (r === null) { fail(`cannot convert \"${s}\" to float`); } return r; },\n"
    "  '[]'(s, i) { const cs = [...s]; let k = num(i); if (k < 0) { k += cs.length; } if (k < 0 || k >= cs.length) { fail(`index ${i} out of range (length ${cs.length})`); } return cs[k]; },\n"
    "  '#'(x) { if (isColl(this.ts[0])) { return BigInt(mh(this.raw[0]).n); } if (T[this.ts[0]].k === 'range') { const v = view(), a = v.getBigInt64(x, true), b = v.getBigInt64(x + 8, true), s = v.getBigInt64(x + 16, true);\n"
    "      const n = s > 0n ? (b - a + s - 1n) / s : (a - b - s - 1n) / -s; return n > 0n ? n : 0n; }\n"
    "    return BigInt(typeof x === 'string' ? enc.encode(x).length : x.length); },\n"
    "  '=='(a, b) { return eqRaw(this.ts[0], this.raw[0], this.raw[1]); },\n"
    "  '!='(a, b) { return !eqRaw(this.ts[0], this.raw[0], this.raw[1]); },\n"
    "  format(f) { let k = 1; const ts = this.ts, raw = this.raw; const arg = () => { const i = k++; return { t: ts[i], r: raw[i], v: toJs(ts[i], raw[i]) }; };\n"
    "    return f.replace(/%%%%|%%([-+ 0#]*)(\\*|\\d*)(?:\\.(\\*|\\d+))?[hlLqjzt]*([diufFeEgGxXoscb])/g, (m, flags, width, prec, c) => {\n"
    "      if (m === '%%%%') { return '%%'; } if (width === '*') { width = String(num(arg().v)); } if (prec === '*') { prec = String(num(arg().v)); }\n"
    "      return cfmt(flags, width, prec, c, arg()); }); },\n"
    "  get_type(x) { return runtimeName(this.ts[0], this.raw[0]); },\n"
    "  min: (a, b) => (typeof a === 'string' ? a < b : num(a) < num(b)) ? a : b,\n"
    "  max: (a, b) => (typeof a === 'string' ? a > b : num(a) > num(b)) ? a : b,\n"
    "  '+': (a, b) => Array.isArray(a) ? [...a, ...b] : typeof a === 'string' ? a + b : arith(a, b, (x, y) => x + y),\n"
    "  '-': (a, b) => arith(a, b, (x, y) => x - y),\n"
    "  '*': (a, b) => typeof a === 'string' || Array.isArray(a) ? NATIVES.repeat(a, b) : arith(a, b, (x, y) => x * y),\n"
    "  '/': (a, b) => typeof a === 'bigint' && typeof b === 'bigint' ? env.idiv(a, b) : num(a) / num(b),\n"
    "  '%%': (a, b) => typeof a === 'bigint' && typeof b === 'bigint' ? env.imod(a, b) : env.fmod(num(a), num(b)),\n"
    "  '**': (a, b) => typeof a === 'bigint' && typeof b === 'bigint' ? env.ipow(a, b) : Math.pow(num(a), num(b)),\n"
    "  '<': (a, b) => num(a) < num(b), '>': (a, b) => num(a) > num(b), '<=': (a, b) => num(a) <= num(b), '>=': (a, b) => num(a) >= num(b),\n"
    "  math_pi: () => Math.PI, math_e: () => Math.E,\n"
    "  math_abs: (x) => typeof x === 'bigint' ? (x < 0n ? -x : x) : Math.abs(x),\n"
    "  math_floor: (x) => BigInt(Math.floor(num(x))), math_ceil: (x) => BigInt(Math.ceil(num(x))),\n"
    "  math_round: (x) => { const v = num(x); return BigInt(Math.sign(v) * Math.round(Math.abs(v))); },\n"
    "  math_trunc: (x) => BigInt(Math.trunc(num(x))),\n"
    "  math_sign: (x) => typeof x === 'bigint' ? (x > 0n ? 1n : x < 0n ? -1n : 0n) : Math.sign(x),\n"
    "  math_hypot2: (x, y) => Math.hypot(num(x), num(y)), math_hypot3: (x, y, z) => Math.hypot(num(x), num(y), num(z)),\n"
    "  math_hypot_array: (a) => Math.hypot(...a.map(num)),\n"
    "  math_clz32: (x) => BigInt(Math.clz32(num(x))), math_imul: (a, b) => BigInt(Math.imul(num(a), num(b))),\n"
    "  math_random: () => Math.random(),\n"
    "  math_random_int: (a, b) => b === undefined ? BigInt(Math.floor(Math.random() * num(a))) : BigInt(num(a) + Math.floor(Math.random() * (num(b) - num(a)))),\n"
    "};\n"
    "Object.assign(NATIVES, COLLECTIONS);\n"
    "for (const k of ['sqrt', 'pow', 'fround', 'cbrt', 'exp', 'expm1', 'log1p', 'log', 'log2', 'log10', 'sin', 'cos', 'tan', 'asin', 'acos',\n"
    "  'atan', 'atan2', 'sinh', 'cosh', 'tanh', 'asinh', 'acosh', 'atanh']) { NATIVES['math_' + k] = (...a) => Math[k](...a.map(num)); }\n"
    "const env = {\n"
    "  out_i64: (t, x) => { line += fmt(t, x, true, 0); },\n"
    "  out_f64: (t, x) => { line += fmt(t, x, true, 0); },\n"
    "  out_i32: (t, x) => { line += fmt(t, x, true, 0); },\n"
    "  out_space: () => { line += ' '; },\n"
    "  out_end: (nl) => { if (nl) { line += '\\n'; } write(line); line = ''; },\n"
    "  str_i64: (t, x) => newStr(fmt(t, x, true, 0)),\n"
    "  str_f64: (t, x) => newStr(fmt(t, x, true, 0)),\n"
    "  str_i32: (t, x) => newStr(fmt(t, x, true, 0)),\n"
    "  concat: (a, b) => { const x = rawStr(a), y = rawStr(b); const r = new Uint8Array(x.length + y.length); r.set(x); r.set(y, x.length); return newRaw(r); },\n"
    "  str_eq: (a, b) => cmpRaw(rawStr(a), rawStr(b)) === 0 ? 1 : 0,\n"
    "  str_cmp: (a, b) => cmpRaw(rawStr(a), rawStr(b)),\n"
    "  idiv: (a, b) => { if (b === 0n) { fail('integer division by zero'); } return a === MIN && b === -1n ? MIN : a / b; },\n"
    "  imod: (a, b) => { if (b === 0n) { fail('integer modulo by zero'); } if (b === -1n) { return 0n; } let r = a %% b;\n"
    "    if (r !== 0n && ((r < 0n) !== (b < 0n))) { r += b; } return r; },\n"
    "  fmod: (a, b) => { let r = a %% b; if (r !== 0 && ((r < 0) !== (b < 0))) { r += b; } return r; },\n"
    "  ipow: (a, e) => { if (e < 0n) { fail(`int ** int needs an exponent >= 0, got ${e}; use a float base: ${a}.0 ** ${e}`); }\n"
    "    let r = 1n, b = BigInt.asUintN(64, a); while (e > 0n) { if (e & 1n) { r = BigInt.asUintN(64, r * b); } b = BigInt.asUintN(64, b * b); e >>= 1n; }\n"
    "    return BigInt.asIntN(64, r); },\n"
    "  fpow: (a, b) => Math.pow(a, b),\n"
    "  arr_slot: (a, i) => { const v = view(); const n = BigInt(v.getInt32(a, true)); const k = i < 0n ? i + n : i;\n"
    "    if (k < 0n || k >= n) { fail(`index ${i} out of range (length ${n})`); } return v.getInt32(a + 8, true) + Number(k) * 8; },\n"
    "  arr_push: (a) => { let v = view(); const n = v.getInt32(a, true), cap = v.getInt32(a + 4, true);\n"
    "    if (n === cap) { const nc = cap ? cap * 2 : 4; const nd = inst.exports.alloc(nc * 8); v = view(); const od = v.getInt32(a + 8, true);\n"
    "      bytes().copyWithin(nd, od, od + n * 8); v.setInt32(a + 4, nc, true); v.setInt32(a + 8, nd, true); }\n"
    "    v.setInt32(a, n + 1, true); return v.getInt32(a + 8, true) + n * 8; },\n"
    "  field: (r, id) => { const v = view(); const shape = v.getInt32(r, true); const n = v.getInt32(shape, true);\n"
    "    for (let i = 0; i < n; ++i) { if (v.getInt32(shape + 4 + 8 * i, true) === id) { return r + 8 + 8 * i; } }\n"
    "    fail(`the record has no field '${NAMES[id]}'`); },\n"
    "  panic: (m) => fail('panic: ' + readStr(m)),\n"
    "  no_match: (l) => fail(`no match arm matched the value (line ${l})`),\n"
    "  fail_msg: (m) => fail(readStr(m)),\n"
    "  gc_alloc: (n) => gcAlloc(n),\n"
    "  native: (id, buf) => {\n"
    "    const [key, ts, rt] = SIGS[id];\n"
    "    const raw = ts.map((t, i) => slot(buf + 8 * i, t));\n"
    "    const r = NATIVES[key].call({ raw, ts, rt }, ...raw.map((x, i) => toJs(ts[i], x)));\n"
    "    if (rt >= 0) { store(buf + 8 * ts.length, rt, r instanceof Raw ? r.v : fromJs(rt, r)); }\n"
    "  },\n"
    "};\n"
    "export async function run(output) {\n"
    "  if (output) { write = output; }\n"
    "  let code;\n"
    "  if (isNode) { const fs = await import('node:fs'); code = fs.readFileSync(new URL('./%s.wasm', import.meta.url)); }\n"
    "  else { code = await (await fetch(new URL('./%s.wasm', import.meta.url))).arrayBuffer(); }\n"
    "  const { instance } = await WebAssembly.instantiate(code, { env });\n"
    "  inst = instance;\n"
    "  try { enter(() => inst.exports.main()); return 0; }\n"
    "  catch (e) { if (!(e instanceof PpError)) { throw e; }\n"
    "    if (isNode) { process.stderr.write('error: ' + e.message + '\\n'); } else { write('error: ' + e.message + '\\n'); } return 1; }\n"
    "}\n"
    "const status = await run();\n"
    "if (isNode) { process.exitCode = status; }\n";

static const char* HTML_PAGE =
    "<!doctype html>\n"
    "<html lang=\"en\">\n"
    "<head>\n"
    "<meta charset=\"utf-8\">\n"
    "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">\n"
    "<title>%s</title>\n"
    "<style>body { font-family: system-ui, sans-serif; margin: 2rem; } pre { background: #f4f4f4; padding: 1rem; }</style>\n"
    "</head>\n"
    "<body>\n"
    "<h1>%s</h1>\n"
    "<p>Compiled from PiPo with <code>pp --target=wasm</code>. Serve this folder over HTTP to run it.</p>\n"
    "<pre id=\"output\"></pre>\n"
    "<script type=\"module\" src=\"./%s.mjs\"></script>\n"
    "</body>\n"
    "</html>\n";

static bool write_file(const char* path, const void* data, size_t len) {
    FILE* f = fopen(path, "wb");
    if (!f) {
        fprintf(stderr, "error: cannot write '%s'\n", path);

        return false;
    }

    const bool ok = fwrite(data, 1, len, f) == len;

    return fclose(f) == 0 && ok;
}

int wasm_compile(VM* vm, Checker* c, const char* base) {
    WasmGen gen;
    memset(&gen, 0, sizeof gen);
    G = &gen;
    gen.vm = vm;
    gen.ch = c;
    sb_init(&gen.typejs);
    sb_puts(&gen.typejs, "");
    if (setjmp(gen.jb)) {
        fprintf(stderr, "%s:%d: error: %s\n", gen.error_file ? gen.error_file : "?", gen.error_line, gen.error);

        return 70;
    }

    if (setjmp(c->jb)) {
        return 70; // the checker found an error while instantiating (already printed)
    }

    // the host functions' types first, then the allocator, then the program
    for (int i = 0; i < H_COUNT; ++i) {
        func_type(HOSTS[i].params, HOSTS[i].result);
    }

    const int alloc_type = func_type("i", "i");
    sb_init(&gen.sigjs);
    sb_puts(&gen.sigjs, "");
    const int tid_int = type_desc(st_prim(ST_INT));
    const int tid_float = type_desc(st_prim(ST_FLOAT));
    const int tid_string = type_desc(st_prim(ST_STRING));
    const int tid_bool = type_desc(st_prim(ST_BOOL));
    const int tid_box = type_desc(st_prim(ST_UNKNOWN));
    gen.g_exit = new_global(NULL, str_cintern(vm, "\x01exit"), T_I32)->index;
    gen.g_exit_rec = new_global(NULL, str_cintern(vm, "\x01exit_rec"), T_I32)->index;
    gen.g_ret[0] = new_global(NULL, str_cintern(vm, "\x01ret_i32"), T_I32)->index;
    gen.g_ret[1] = new_global(NULL, str_cintern(vm, "\x01ret_i64"), T_I64)->index;
    gen.g_ret[2] = new_global(NULL, str_cintern(vm, "\x01ret_f64"), T_F64)->index;
    gen.g_scope = new_global(NULL, str_cintern(vm, "\x01scope"), T_I32)->index;
    const int main_func = module_init(c->main);
    for (bool more = true; more;) {
        more = false;
        for (int i = 0; i < gen.nfuncs; ++i) {
            WFunc* f = gen.funcs[i];
            if (!f->done && !f->nested && f->inst) {
                CModule* m = f->inst->fn && f->inst->fn->module ? f->inst->fn->module : c->main;
                compile_body(f, m, NULL, NULL);
                more = true;
            }
        }
    }

    // the type tests, now that every boxed type is known: a chain of id comparisons
    for (int i = 0; i < gen.ntests; ++i) {
        Bytes* b = &gen.test_funcs[i]->body;
        uleb(b, 0);
        for (int k = 0; k < gen.nboxed; ++k) {
            if (st_fit(gen.tests[i], gen.tdescs[gen.boxed[k]], NULL) <= 0) {
                continue;
            }

            put(b, W_LOCAL_GET);
            uleb(b, 0);
            put(b, W_I32_CONST);
            sleb(b, gen.boxed[k]);
            put(b, W_I32_EQ);
            put(b, W_IF);
            put(b, T_NONE);
            put(b, W_I32_CONST);
            sleb(b, 1);
            put(b, W_RETURN);
            put(b, W_END);
        }

        put(b, W_I32_CONST);
        sleb(b, 0);
        put(b, W_END);
    }

    // a function no code reached (a local function never declared on a path that ran): a stub
    for (int i = 0; i < gen.nfuncs; ++i) {
        WFunc* f = gen.funcs[i];
        if (!f->done && f->body.len == 0) {
            uleb(&f->body, 0);
            put(&f->body, W_UNREACHABLE);
            put(&f->body, W_END);
        }
    }

    Bytes out = {0};
    static const uint8_t header[] = {0x00, 0x61, 0x73, 0x6D, 0x01, 0x00, 0x00, 0x00};
    put_all(&out, header, sizeof header);
    Bytes s = {0};
    uleb(&s, (uint64_t)gen.ntypes);
    for (int i = 0; i < gen.ntypes; ++i) {
        put(&s, 0x60);
        uleb(&s, gen.types[i].params.len);
        put_all(&s, gen.types[i].params.data, gen.types[i].params.len);
        uleb(&s, gen.types[i].results.len);
        put_all(&s, gen.types[i].results.data, gen.types[i].results.len);
    }

    section(&out, 1, &s);
    uleb(&s, H_COUNT);
    for (int i = 0; i < H_COUNT; ++i) {
        put_name(&s, "env");
        put_name(&s, HOSTS[i].name);
        put(&s, 0x00);
        uleb(&s, (uint64_t)func_type(HOSTS[i].params, HOSTS[i].result));
    }

    section(&out, 2, &s);
    uleb(&s, (uint64_t)gen.nfuncs + 1);
    uleb(&s, (uint64_t)alloc_type);
    for (int i = 0; i < gen.nfuncs; ++i) {
        uleb(&s, (uint64_t)gen.funcs[i]->type);
    }

    section(&out, 3, &s);
    uleb(&s, 1);
    put(&s, 0x70); // funcref
    put(&s, 0x00);
    uleb(&s, (uint64_t)gen.ntable);
    section(&out, 4, &s);
    const size_t data_end = DATA_BASE + gen.data.len;
    uleb(&s, 1);
    put(&s, 0x00);
    uleb(&s, data_end / 65536 + 2);
    section(&out, 5, &s);
    uleb(&s, (uint64_t)gen.nglobals + 1);
    put(&s, T_I32);
    put(&s, 0x01);
    put(&s, W_I32_CONST);
    sleb(&s, (int64_t)((data_end + 7) & ~(size_t)7));
    put(&s, W_END);
    for (int i = 0; i < gen.nglobals; ++i) {
        const uint8_t vt = gen.globals[i].vt;
        put(&s, vt);
        put(&s, 0x01);
        if (vt == T_I64) {
            put(&s, W_I64_CONST);
            sleb(&s, 0);
        } else if (vt == T_F64) {
            const double z = 0;
            put(&s, W_F64_CONST);
            put_all(&s, &z, 8);
        } else {
            put(&s, W_I32_CONST);
            sleb(&s, 0);
        }

        put(&s, W_END);
    }

    section(&out, 6, &s);
    int nroots = 0;
    for (int i = 0; i < gen.nglobals; ++i) {
        nroots += gen.globals[i].vt == T_I32 ? 1 : 0;
    }

    uleb(&s, (uint64_t)(4 + nroots));
    for (int i = 0; i < gen.nglobals; ++i) {
        if (gen.globals[i].vt == T_I32) {
            char gname[24];
            snprintf(gname, sizeof gname, "g%d", gen.globals[i].index);
            put_name(&s, gname);
            put(&s, 0x03);
            uleb(&s, (uint64_t)gen.globals[i].index);
        }
    }

    put_name(&s, "table");
    put(&s, 0x01);
    uleb(&s, 0);
    put_name(&s, "memory");
    put(&s, 0x02);
    uleb(&s, 0);
    put_name(&s, "alloc");
    put(&s, 0x00);
    uleb(&s, H_COUNT);
    put_name(&s, "main");
    put(&s, 0x00);
    uleb(&s, (uint64_t)main_func);
    section(&out, 7, &s);
    // the table's functions
    uleb(&s, 1);
    put(&s, 0x00);
    put(&s, W_I32_CONST);
    sleb(&s, 0);
    put(&s, W_END);
    uleb(&s, (uint64_t)gen.ntable);
    for (int i = 0; i < gen.ntable; ++i) {
        uleb(&s, (uint64_t)gen.table[i]);
    }

    section(&out, 9, &s);
    uleb(&s, (uint64_t)gen.nfuncs + 1);
    Bytes a = {0};
    alloc_body(&a);
    uleb(&s, a.len);
    put_all(&s, a.data, a.len);
    free(a.data);
    for (int i = 0; i < gen.nfuncs; ++i) {
        uleb(&s, gen.funcs[i]->body.len);
        put_all(&s, gen.funcs[i]->body.data, gen.funcs[i]->body.len);
    }

    section(&out, 10, &s);
    uleb(&s, 1);
    put(&s, 0x00);
    put(&s, W_I32_CONST);
    sleb(&s, DATA_BASE);
    put(&s, W_END);
    uleb(&s, gen.data.len);
    put_all(&s, gen.data.data, gen.data.len);
    section(&out, 11, &s);

    // the files: base.wasm, base.mjs, base.html
    const char* name = base;
    for (const char* p = base; *p; ++p) {
        if (*p == '/' || *p == '\\') {
            name = p + 1;
        }
    }

    StrBuf path;
    sb_init(&path);
    sb_printf(&path, "%s.wasm", base);
    if (!write_file(path.data, out.data, out.len)) {
        return 73;
    }

    StrBuf names;
    sb_init(&names);
    sb_puts(&names, "");
    for (int i = 0; i < gen.nnames; ++i) {
        if (i) {
            sb_puts(&names, ", ");
        }

        js_string(&names, gen.names[i]->chars);
    }

    // the names of the functions in the table, for printing function values
    sb_puts(&names, "];\nconst FNAMES = [");
    for (int i = 0; i < gen.ntable; ++i) {
        const char* fname = "lambda";
        for (int k = 0; k < gen.nfuncs; ++k) {
            const WFunc* f = gen.funcs[k];
            if (f->index == gen.table[i] && f->inst && f->inst->fn && f->inst->fn->name) {
                fname = f->inst->fn->name->chars;
            }
        }

        if (i) {
            sb_puts(&names, ", ");
        }

        js_string(&names, fname);
    }

    StrBuf js;
    sb_init(&js);
    sb_printf(&js, JS_HOST, name, name, name, gen.typejs.data, names.data, gen.sigjs.data, tid_int, tid_float, tid_string,
              tid_bool, tid_box, (int)((data_end + 7) & ~(size_t)7), name, name);
    path.len = 0;
    sb_printf(&path, "%s.mjs", base);
    if (!write_file(path.data, js.data, js.len)) {
        return 73;
    }

    StrBuf html;
    sb_init(&html);
    sb_printf(&html, HTML_PAGE, name, name, name);
    path.len = 0;
    sb_printf(&path, "%s.html", base);
    if (!write_file(path.data, html.data, html.len)) {
        return 73;
    }

    fprintf(stderr, "wrote %s.wasm (%zu bytes), %s.mjs and %s.html\n", base, out.len, base, base);
    sb_free(&path);
    sb_free(&names);
    sb_free(&js);
    sb_free(&html);
    free(out.data);

    return 0;
}

