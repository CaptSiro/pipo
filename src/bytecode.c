#include "backend.h"
#include "bytecode.h"



// Compiles the instances the checker produced (see check.h) to bytecode (bytecode.h).
// Every call, operator and index was resolved by the checker, so the compiler only looks
// the target up in the instance's NodeInfo table. Constructs the VM does not run yet are
// refused with their line, so a program either compiles completely or not at all.

const OpInfo BC_OPS[OP_COUNT] = {
    [OP_INT] = {"INT", 1},
    [OP_CONST] = {"CONST", 1},
    [OP_TRUE] = {"TRUE", 0},
    [OP_FALSE] = {"FALSE", 0},
    [OP_VOID] = {"VOID", 0},
    [OP_POP] = {"POP", 0},
    [OP_DUP] = {"DUP", 0},
    [OP_LOAD] = {"LOAD", 1},
    [OP_STORE] = {"STORE", 1},
    [OP_GLOAD] = {"GLOAD", 1},
    [OP_GSTORE] = {"GSTORE", 1},
    [OP_CAP] = {"CAP", 1},
    [OP_BOX] = {"BOX", 1},
    [OP_CELL_GET] = {"CELL_GET", 0},
    [OP_CELL_SET] = {"CELL_SET", 0},
    [OP_ADD_I] = {"ADD_I", 0},
    [OP_SUB_I] = {"SUB_I", 0},
    [OP_MUL_I] = {"MUL_I", 0},
    [OP_DIV_I] = {"DIV_I", 0},
    [OP_MOD_I] = {"MOD_I", 0},
    [OP_NEG_I] = {"NEG_I", 0},
    [OP_POW_I] = {"POW_I", 0},
    [OP_ADD_F] = {"ADD_F", 0},
    [OP_SUB_F] = {"SUB_F", 0},
    [OP_MUL_F] = {"MUL_F", 0},
    [OP_DIV_F] = {"DIV_F", 0},
    [OP_MOD_F] = {"MOD_F", 0},
    [OP_NEG_F] = {"NEG_F", 0},
    [OP_POW_F] = {"POW_F", 0},
    [OP_I2F] = {"I2F", 0},
    [OP_I2F_UNDER] = {"I2F_UNDER", 0},
    [OP_EQ_I] = {"EQ_I", 0},
    [OP_NE_I] = {"NE_I", 0},
    [OP_LT_I] = {"LT_I", 0},
    [OP_GT_I] = {"GT_I", 0},
    [OP_LE_I] = {"LE_I", 0},
    [OP_GE_I] = {"GE_I", 0},
    [OP_EQ_F] = {"EQ_F", 0},
    [OP_NE_F] = {"NE_F", 0},
    [OP_LT_F] = {"LT_F", 0},
    [OP_GT_F] = {"GT_F", 0},
    [OP_LE_F] = {"LE_F", 0},
    [OP_GE_F] = {"GE_F", 0},
    [OP_EQ_S] = {"EQ_S", 0},
    [OP_NE_S] = {"NE_S", 0},
    [OP_LT_S] = {"LT_S", 0},
    [OP_GT_S] = {"GT_S", 0},
    [OP_LE_S] = {"LE_S", 0},
    [OP_GE_S] = {"GE_S", 0},
    [OP_EQ_B] = {"EQ_B", 0},
    [OP_NE_B] = {"NE_B", 0},
    [OP_NOT] = {"NOT", 0},
    [OP_CONCAT] = {"CONCAT", 0},
    [OP_LEN_S] = {"LEN_S", 0},
    [OP_LEN_A] = {"LEN_A", 0},
    [OP_VALUES_EQ] = {"VALUES_EQ", 0},
    [OP_JUMP] = {"JUMP", 1},
    [OP_JUMP_FALSE] = {"JUMP_FALSE", 1},
    [OP_JUMP_TRUE] = {"JUMP_TRUE", 1},
    [OP_AND] = {"AND", 1},
    [OP_OR] = {"OR", 1},
    [OP_CALL] = {"CALL", 3},
    [OP_CALL_NATIVE] = {"CALL_NATIVE", 3},
    [OP_CALL_VALUE] = {"CALL_VALUE", 2},
    [OP_RETURN] = {"RETURN", 0},
    [OP_ARRAY] = {"ARRAY", 1},
    [OP_TUPLE] = {"TUPLE", 1},
    [OP_OBJECT] = {"OBJECT", 0},
    [OP_FIELD_INIT] = {"FIELD_INIT", 1},
    [OP_GET_FIELD] = {"GET_FIELD", 2},
    [OP_SET_FIELD] = {"SET_FIELD", 2},
    [OP_INDEX] = {"INDEX", 1},
    [OP_INDEX_SET] = {"INDEX_SET", 1},
    [OP_ENUM] = {"ENUM", 3},
    [OP_IS_VARIANT] = {"IS_VARIANT", 2},
    [OP_PAYLOAD] = {"PAYLOAD", 1},
    [OP_IS_TUPLE] = {"IS_TUPLE", 1},
    [OP_TUPLE_GET] = {"TUPLE_GET", 1},
    [OP_AS] = {"AS", 1},
    [OP_TAG] = {"TAG", 1},
    [OP_ITER_INIT] = {"ITER_INIT", 1},
    [OP_ITER_NEXT] = {"ITER_NEXT", 4},
    [OP_CLOSURE] = {"CLOSURE", 2},
    [OP_RUN_MODULE] = {"RUN_MODULE", 1},
    [OP_LINE] = {"LINE", 1},
    [OP_NO_MATCH] = {"NO_MATCH", 1},
    [OP_FAIL] = {"FAIL", 1},
    [OP_DISPATCH] = {"DISPATCH", 3},
    [OP_SCOPE_CALL] = {"SCOPE_CALL", 3},
    [OP_SCOPE_EXIT] = {"SCOPE_EXIT", 1},
    [OP_DEFER] = {"DEFER", 0},
    [OP_SUSPEND] = {"SUSPEND", 0},
    [OP_GEN_END] = {"GEN_END", 1},
    [OP_JUMP_ITERABLE] = {"JUMP_ITERABLE", 1},
};



typedef struct {
    ObjString* name;
    int slot;
    bool boxed;
    int depth;
} BcLocal;

typedef struct BcLoop {
    struct BcLoop* prev;
    int* breaks;
    int nbreaks;
    int capbreaks;
    int* conts;
    int nconts;
    int capconts;
} BcLoop;

typedef struct {
    ObjString* name;
    bool from_local; // a local of the enclosing function, else one of its captures
    int index;
} BcCap;

typedef struct Comp {
    BcProgram* prog;
    BcFunc* fn;
    CInstance* inst;
    struct Comp* outer;
    BcModule* mod;
    BcLocal* locals;
    int nlocals;
    int caplocals;
    int depth;
    int next_slot;
    BcCap* caps;
    int ncaps;
    int capcaps;
    BcLoop* loop;
    bool module_top;     // declarations at depth 0 are the module's globals
    NameList boxed;      // names used inside nested functions: locals with these names live in cells
    SType* ret;          // the instance's return type
} Comp;

static void compile_function(BcProgram* prog, BcFunc* f, Comp* outer, BcModule* mod);

static bool is_name_eq(const ObjString* s, const char* text) {
    return s && str_equals((ObjString*)s, text);
}

static void nl_push_raw(NodeList* l, Node* n) {
    GROW(l->items, l->count, l->cap);
    l->items[l->count++] = n;
}
static void expr(Comp* c, Node* n);
static void stmt(Comp* c, Node* n);
static void emit_return(Comp* c);
static void scope_call(Comp* c, Node* n, bool value);
static BcModule* module_of(BcProgram* prog, CModule* m);



// ---------------------------------------------------------------- errors and emitting

static PP_NORETURN void fail(Comp* c, int line, const char* fmt, ...) {
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    c->prog->error = pp_strdup(buf);
    c->prog->error_line = line;
    c->prog->error_file = c->fn->file;
    longjmp(*c->prog->err, 1);
}

static void emit(Comp* c, int32_t w) {
    BcFunc* f = c->fn;
    if (f->count >= f->cap) {
        f->cap = f->cap ? f->cap * 2 : 64;
        f->code = pp_realloc(f->code, sizeof(int32_t) * (size_t)f->cap);
    }

    f->code[f->count++] = w;
}

static void op0(Comp* c, OpCode op) {
    emit(c, op);
}

static void op1(Comp* c, OpCode op, int a) {
    emit(c, op);
    emit(c, a);
}

static void op2(Comp* c, OpCode op, int a, int b) {
    emit(c, op);
    emit(c, a);
    emit(c, b);
}

static void op3(Comp* c, OpCode op, int a, int b, int d) {
    emit(c, op);
    emit(c, a);
    emit(c, b);
    emit(c, d);
}

// emits a jump and returns where its target goes
static int jump(Comp* c, OpCode op) {
    emit(c, op);
    emit(c, -1);

    return c->fn->count - 1;
}

static void patch(Comp* c, int at) {
    c->fn->code[at] = c->fn->count;
}

static void add_patch(int** list, int* count, int* cap, int at) {
    GROW(*list, *count, *cap);
    (*list)[(*count)++] = at;
}

static void keep(BcProgram* prog, Value v) {
    if (IS_OBJ(v)) {
        array_push(prog->roots, v);
    }
}

static int add_const(BcProgram* prog, Value v) {
    for (int i = 0; i < prog->nconsts; ++i) {
        const Value k = prog->consts[i];
        if (k.kind == v.kind && k.as.i == v.as.i && (v.kind != VAL_FLOAT || k.as.f == v.as.f)) {
            return i;
        }
    }

    keep(prog, v);
    GROW(prog->consts, prog->nconsts, prog->capconsts);
    prog->consts[prog->nconsts] = v;

    return prog->nconsts++;
}

static int add_string(BcProgram* prog, const char* s) {
    return add_const(prog, obj_val(str_cintern(prog->vm, s)));
}

static int add_native(BcProgram* prog, NativeFn fn, const char* name) {
    for (int i = 0; i < prog->nnatives; ++i) {
        if (prog->natives[i].fn == fn) {
            return i;
        }
    }

    GROW(prog->natives, prog->nnatives, prog->capnatives);
    prog->natives[prog->nnatives].fn = fn;
    prog->natives[prog->nnatives].name = name;

    return prog->nnatives++;
}

static int add_type(BcProgram* prog, SType* t) {
    GROW(prog->types, prog->ntypes, prog->captypes);
    prog->types[prog->ntypes] = t;

    return prog->ntypes++;
}



// ---------------------------------------------------------------- program tables

static BcFunc* func_for(BcProgram* prog, CInstance* inst) {
    for (int i = 0; i < prog->nfuncs; ++i) {
        if (prog->funcs[i]->inst == inst) {
            return prog->funcs[i];
        }
    }

    BcFunc* f = pp_calloc(1, sizeof(BcFunc));
    f->inst = inst;
    f->index = prog->nfuncs;
    if (inst && inst->fn) {
        f->name = inst->fn->name;
        f->file = inst->fn->sig && inst->fn->sig->file ? inst->fn->sig->file
                  : inst->fn->module                    ? inst->fn->module->path
                                                        : "?";
        f->line = inst->fn->sig ? inst->fn->sig->line : 0;
        f->nparams = inst->nparams;
        f->generator = inst->fn->sig && inst->fn->sig->generator;
    }

    GROW(prog->funcs, prog->nfuncs, prog->capfuncs);
    prog->funcs[prog->nfuncs++] = f;

    return f;
}

static int enum_index(BcProgram* prog, TDecl* d) {
    for (int i = 0; i < prog->nenums; ++i) {
        if (prog->enum_decls[i] == d) {
            return i;
        }
    }

    VM* vm = prog->vm;
    ObjEnum* en;
    if (d == prog->checker->option) {
        en = vm->option_enum;
    } else if (d == prog->checker->error) {
        en = vm->error_enum;
    } else {
        en = vm_enum_new(vm, d->name, d->nvariants);
        keep(prog, obj_val(en));
        for (int i = 0; i < d->nvariants; ++i) {
            // the associated value: written as a literal, or the variant's index
            Value value = int_val(i);
            vm_enum_variant(vm, en, i, d->variants[i].name, d->variants[i].is_ctor, d->variants[i].npayload, value);
        }
    }

    if (prog->nenums >= prog->capenums) {
        prog->capenums = prog->capenums ? prog->capenums * 2 : 8;
        prog->enums = pp_realloc(prog->enums, sizeof(ObjEnum*) * (size_t)prog->capenums);
        prog->enum_decls = pp_realloc(prog->enum_decls, sizeof(TDecl*) * (size_t)prog->capenums);
    }

    prog->enums[prog->nenums] = en;
    prog->enum_decls[prog->nenums] = d;

    return prog->nenums++;
}

// an enum declared with values (enum string X { A = "a" }): the values are constants of the declaration
static void enum_values(Comp* c, Node* n, TDecl* d) {
    const int e = enum_index(c->prog, d);
    ObjEnum* en = c->prog->enums[e];
    for (int i = 0; i < n->as.enum_decl.nvariants && i < en->nvariants; ++i) {
        const Node* v = n->as.enum_decl.variants[i].value;
        if (!v) {
            continue;
        }

        switch (v->kind) {
            case N_INT: en->variants[i].value = int_val(v->as.ival); break;
            case N_FLOAT: en->variants[i].value = float_val(v->as.fval); break;
            case N_STRING: en->variants[i].value = obj_val(v->as.str); break;
            case N_BOOL: en->variants[i].value = bool_val(v->as.bval); break;
            default: fail(c, v->line, "the value of an enum variant must be a literal in compiled code");
        }
    }
}

static int typedef_index(BcProgram* prog, TDecl* d) {
    for (int i = 0; i < prog->ntypedefs; ++i) {
        if (prog->typedef_decls[i] == d) {
            return i;
        }
    }

    ObjTypeDef* td = vm_typedef_new(prog->vm, d->name);
    keep(prog, obj_val(td));
    if (prog->ntypedefs >= prog->captypedefs) {
        prog->captypedefs = prog->captypedefs ? prog->captypedefs * 2 : 8;
        prog->typedefs = pp_realloc(prog->typedefs, sizeof(ObjTypeDef*) * (size_t)prog->captypedefs);
        prog->typedef_decls = pp_realloc(prog->typedef_decls, sizeof(TDecl*) * (size_t)prog->captypedefs);
    }

    prog->typedefs[prog->ntypedefs] = td;
    prog->typedef_decls[prog->ntypedefs] = d;

    return prog->ntypedefs++;
}

// the C body of a function: a builtin / native module's, or a library module's `native function`
static int native_of(Comp* c, CFunc* f, int line) {
    if (f->native) {
        return add_native(c->prog, f->native->fn, f->native->sig->name->chars);
    }

    if (f->impl_name && f->module && f->module->path && strcmp(f->module->path, "<prelude>") == 0) {
        const NativeImpl* impls = vm_native_impls(c->prog->vm, PP_PRELUDE_NATIVES);
        for (int i = 0; impls && impls[i].name; ++i) {
            if (str_equals(f->impl_name, impls[i].name)) {
                return add_native(c->prog, impls[i].fn, f->name->chars);
            }
        }
    }

    if (f->impl_name && f->module && f->module->path) {
        // the library module's path below lib/ names its C half: lib/os/fs.pp is "os/fs"
        const char* p = f->module->path;
        const char* rel = NULL;
        for (const char* s = p; *s; ++s) {
            if ((s[0] == '/' || s[0] == '\\') && strncmp(s + 1, "lib", 3) == 0 && (s[4] == '/' || s[4] == '\\')) {
                rel = s + 5;
            }
        }

        if (rel) {
            char path[256];
            snprintf(path, sizeof path, "%s", rel);
            const size_t n = strlen(path);
            if (n > 3 && strcmp(path + n - 3, ".pp") == 0) {
                path[n - 3] = '\0';
            }

            for (char* s = path; *s; ++s) {
                if (*s == '\\') {
                    *s = '/';
                }
            }

            const NativeImpl* impls = vm_native_impls(c->prog->vm, path);
            for (int i = 0; impls && impls[i].name; ++i) {
                if (str_equals(f->impl_name, impls[i].name)) {
                    return add_native(c->prog, impls[i].fn, f->name->chars);
                }
            }
        }
    }

    fail(c, line, "native function '%s' has no C implementation the bytecode target can call", f->name->chars);
}



// ---------------------------------------------------------------- types

static NodeInfo* info(Comp* c, Node* n) {
    return instance_info(c->inst, n);
}

static SType* type_of_node(Comp* c, Node* n) {
    const NodeInfo* i = info(c, n);
    if (!i || !i->type) {
        fail(c, n->line, "internal: no static type for this expression");
    }

    if (st_is_dyn(i->type)) {
        fail(c, n->line, "this expression has no static type (%s); compile it with --target=interp",
             c->inst->dyn_reason ? c->inst->dyn_reason : "dynamic");
    }

    return i->type;
}

// a value of type `from` where one of type `to` is expected: ints become floats
static void convert(Comp* c, const SType* from, const SType* to) {
    if (from && to && from->kind == ST_INT && to->kind == ST_FLOAT) {
        op0(c, OP_I2F);
    }
}

static void expr_to(Comp* c, Node* n, const SType* to) {
    expr(c, n);
    convert(c, type_of_node(c, n), to);
}

static bool is_nominal(const SType* t) {
    return t && t->kind == ST_RECORD && t->decl;
}

// objects declared (or passed, or returned) as a record type are tagged with it, like the interpreter does
static void tag_as(Comp* c, const SType* t) {
    if (is_nominal(t)) {
        op1(c, OP_TAG, typedef_index(c->prog, t->decl));
    } else if (t && t->kind == ST_ARRAY && is_nominal(t->elem)) {
        op1(c, OP_TAG, typedef_index(c->prog, t->elem->decl));
    }
}



// ---------------------------------------------------------------- locals

static bool boxed_name(Comp* c, ObjString* name) {
    return names_has(&c->boxed, name);
}

static int new_slot(Comp* c) {
    const int s = c->next_slot++;
    if (c->next_slot > c->fn->nlocals) {
        c->fn->nlocals = c->next_slot;
    }

    return s;
}

static BcLocal* add_local(Comp* c, ObjString* name) {
    GROW(c->locals, c->nlocals, c->caplocals);
    BcLocal* l = &c->locals[c->nlocals++];
    l->name = name;
    l->slot = new_slot(c);
    l->boxed = boxed_name(c, name);
    l->depth = c->depth;

    return l;
}

static int global_of(BcModule* m, ObjString* name) {
    for (int i = m->ngnames - 1; i >= 0; --i) {
        if (m->gnames[i] == name || str_equals(m->gnames[i], name->chars)) {
            return m->gindex[i];
        }
    }

    return -1;
}

static void add_global_name(BcModule* m, ObjString* name, int index) {
    if (m->ngnames >= m->capgnames) {
        m->capgnames = m->capgnames ? m->capgnames * 2 : 16;
        m->gnames = pp_realloc(m->gnames, sizeof(ObjString*) * (size_t)m->capgnames);
        m->gindex = pp_realloc(m->gindex, sizeof(int) * (size_t)m->capgnames);
    }

    m->gnames[m->ngnames] = name;
    m->gindex[m->ngnames] = index;
    m->ngnames++;
}

static int new_global(BcProgram* prog, BcModule* m, ObjString* name) {
    const int g = prog->nglobals++;
    add_global_name(m, name, g);

    return g;
}

// A variable being declared: a global at the top of a module, else a local (in a cell when
// a nested function uses its name). The value to store is on the stack.
static void declare_store(Comp* c, ObjString* name) {
    if (c->module_top && c->depth == 0) {
        op1(c, OP_GSTORE, new_global(c->prog, c->mod, name));

        return;
    }

    const BcLocal* l = add_local(c, name);
    op1(c, OP_STORE, l->slot);
    if (l->boxed) {
        op1(c, OP_BOX, l->slot);
    }
}

static void begin_scope(Comp* c) {
    c->depth++;
}

static void end_scope(Comp* c) {
    c->depth--;
    while (c->nlocals > 0 && c->locals[c->nlocals - 1].depth > c->depth) {
        c->nlocals--;
    }

    // slots are not reused: a cell captured from a dead slot stays valid either way
}

static BcLocal* find_local(Comp* c, ObjString* name) {
    for (int i = c->nlocals - 1; i >= 0; --i) {
        if (c->locals[i].name == name || str_equals(c->locals[i].name, name->chars)) {
            return &c->locals[i];
        }
    }

    return NULL;
}

// the index of name among c's captures, adding it; -1 when no enclosing function has it
static int find_capture(Comp* c, ObjString* name) {
    for (int i = 0; i < c->ncaps; ++i) {
        if (c->caps[i].name == name || str_equals(c->caps[i].name, name->chars)) {
            return i;
        }
    }

    if (!c->outer) {
        return -1;
    }

    BcCap cap;
    cap.name = name;
    const BcLocal* l = find_local(c->outer, name);
    if (l) {
        if (!l->boxed) {
            return -1;
        }

        cap.from_local = true;
        cap.index = l->slot;
    } else {
        const int k = find_capture(c->outer, name);
        if (k < 0) {
            return -1;
        }

        cap.from_local = false;
        cap.index = k;
    }

    GROW(c->caps, c->ncaps, c->capcaps);
    c->caps[c->ncaps] = cap;

    return c->ncaps++;
}

typedef enum { VAR_NONE, VAR_LOCAL, VAR_CAPTURE, VAR_GLOBAL } VarKind;

static VarKind find_var(Comp* c, ObjString* name, int* index, bool* boxed) {
    const BcLocal* l = find_local(c, name);
    if (l) {
        *index = l->slot;
        *boxed = l->boxed;

        return VAR_LOCAL;
    }

    const int k = find_capture(c, name);
    if (k >= 0) {
        *index = k;
        *boxed = true;

        return VAR_CAPTURE;
    }

    const int g = global_of(c->mod, name);
    if (g >= 0) {
        *index = g;
        *boxed = false;

        return VAR_GLOBAL;
    }

    return VAR_NONE;
}

static bool load_var(Comp* c, ObjString* name) {
    int index;
    bool boxed;
    switch (find_var(c, name, &index, &boxed)) {
        case VAR_LOCAL: op1(c, OP_LOAD, index); break;
        case VAR_CAPTURE: op1(c, OP_CAP, index); break;
        case VAR_GLOBAL: op1(c, OP_GLOAD, index); return true;
        case VAR_NONE: return false;
    }

    if (boxed) {
        op0(c, OP_CELL_GET);
    }

    return true;
}

// stores the value on top of the stack into an existing variable
static void store_var(Comp* c, ObjString* name, int line) {
    int index;
    bool boxed;
    switch (find_var(c, name, &index, &boxed)) {
        case VAR_LOCAL:
            if (boxed) {
                op1(c, OP_LOAD, index);
                op0(c, OP_CELL_SET);

                return;
            }

            op1(c, OP_STORE, index);

            return;
        case VAR_CAPTURE:
            op1(c, OP_CAP, index);
            op0(c, OP_CELL_SET);

            return;
        case VAR_GLOBAL: op1(c, OP_GSTORE, index); return;
        case VAR_NONE: fail(c, line, "the bytecode target cannot find the variable '%s'", name->chars);
    }
}



// ---------------------------------------------------------------- calls

static bool is_nested(Comp* c, CFunc* f);

// pushes the arguments (receiver first for a dot call), converted to the callee's parameter types
static void push_args(Comp* c, Node* recv, NodeList* args, CInstance* target) {
    int i = 0;
    if (recv) {
        expr_to(c, recv, target && target->nparams > 0 ? target->params[0] : NULL);
        i++;
    }

    for (int k = 0; k < args->count; ++k, ++i) {
        expr_to(c, args->items[k], target && i < target->nparams ? target->params[i] : NULL);
    }
}

static int compare_opcode(int cop) {
    static const int map[][2] = {
        {COP_EQ_I, OP_EQ_I}, {COP_NE_I, OP_NE_I}, {COP_LT_I, OP_LT_I}, {COP_GT_I, OP_GT_I}, {COP_LE_I, OP_LE_I},
        {COP_GE_I, OP_GE_I}, {COP_EQ_F, OP_EQ_F}, {COP_NE_F, OP_NE_F}, {COP_LT_F, OP_LT_F}, {COP_GT_F, OP_GT_F},
        {COP_LE_F, OP_LE_F}, {COP_GE_F, OP_GE_F}, {COP_EQ_S, OP_EQ_S}, {COP_NE_S, OP_NE_S}, {COP_LT_S, OP_LT_S},
        {COP_GT_S, OP_GT_S}, {COP_LE_S, OP_LE_S}, {COP_GE_S, OP_GE_S}, {COP_EQ_B, OP_EQ_B}, {COP_NE_B, OP_NE_B},
        {COP_ADD_I, OP_ADD_I}, {COP_SUB_I, OP_SUB_I}, {COP_MUL_I, OP_MUL_I}, {COP_DIV_I, OP_DIV_I},
        {COP_MOD_I, OP_MOD_I}, {COP_POW_I, OP_POW_I}, {COP_ADD_F, OP_ADD_F}, {COP_SUB_F, OP_SUB_F},
        {COP_MUL_F, OP_MUL_F}, {COP_DIV_F, OP_DIV_F}, {COP_MOD_F, OP_MOD_F}, {COP_POW_F, OP_POW_F},
        {COP_CONCAT, OP_CONCAT}, {COP_NEG_I, OP_NEG_I}, {COP_NEG_F, OP_NEG_F}, {COP_NOT, OP_NOT},
        {COP_LEN_S, OP_LEN_S}, {COP_LEN_A, OP_LEN_A}, {-1, -1},
    };
    for (int i = 0; map[i][0] >= 0; ++i) {
        if (map[i][0] == cop) {
            return map[i][1];
        }
    }

    return -1;
}

// the candidates of a call that needs dynamic dispatch, most specific first
static int dispatch_table(Comp* c, const CCall* call, ObjString* name, SType** sargs, int argc, int line) {
    BcProgram* prog = c->prog;
    Checker* ch = prog->checker;
    // a requirement of an interface stands for every function an impl recorded under its name
    CFunc* single[1] = {call->func};
    CFunc** given = call->cands ? call->cands : single;
    const int ngiven = call->cands ? call->ncands : 1;
    CFunc** cands = pp_calloc((size_t)(ngiven + ch->nimpl_fns) + 1, sizeof(CFunc*));
    int ncands = 0;
    for (int g = 0; g < ngiven; ++g) {
        CFunc* f = given[g];
        if (!f) {
            continue;
        }

        for (int i = 0; f->kind == CF_REQ && i < ch->nimpl_fns; ++i) {
            if (!str_equals(ch->impl_fns[i].name, f->name->chars)) {
                continue;
            }

            bool seen = false;
            for (int k = 0; k < ncands; ++k) {
                seen = seen || cands[k] == ch->impl_fns[i].fn;
            }

            if (!seen) {
                cands[ncands++] = ch->impl_fns[i].fn;
            }
        }

        bool seen = f->kind == CF_REQ;
        for (int k = 0; k < ncands; ++k) {
            seen = seen || cands[k] == f;
        }

        if (!seen) {
            cands[ncands++] = f;
        }
    }

    BcDispatch d = {0};
    d.name = name;
    d.cands = pp_calloc((size_t)ncands + 1, sizeof(BcCand));
    for (int i = 0; i < ncands; ++i) {
        CFunc* f = cands[i];
        if (f->kind == CF_REQ) {
            continue; // the requirement itself: the implementations are the candidates
        }

        BcCand* bc = &d.cands[d.ncands];
        CInstance* inst = checker_declared_instance(ch, f, sargs, argc, &bc->params, &bc->nparams);
        bc->variadic = f->sig && f->sig->variadic;
        if (f->kind == CF_USER) {
            if (!inst) {
                fail(c, line, "'%s' is called through dynamic dispatch, which needs its parameters typed (it is generic)",
                     f->name->chars);
            }

            if (is_nested(c, f)) {
                fail(c, line, "a function declared inside a function cannot be called through dynamic dispatch yet");
            }

            bc->native = false;
            bc->index = func_for(prog, inst)->index;
        } else if (f->kind == CF_NATIVE) {
            bc->native = true;
            bc->index = native_of(c, f, line);
        } else {
            fail(c, line, "the bytecode target cannot dispatch to '%s'", f->name->chars);
        }

        d.ncands++;
    }

    if (d.ncands == 0) {
        fail(c, line, "no implementation of '%s' is known to dispatch to", name->chars);
    }

    (void)argc;
    GROW(prog->dispatches, prog->ndispatches, prog->capdispatches);
    prog->dispatches[prog->ndispatches] = d;

    return prog->ndispatches++;
}

static bool needs_dispatch(const CCall* call) {
    return call->dispatch || (call->func && call->func->kind == CF_REQ);
}

static bool float_op(int cop) {
    return (cop >= COP_ADD_F && cop <= COP_POW_F) || (cop >= COP_EQ_F && cop <= COP_GE_F);
}

// a call the checker resolved. operands: the argument nodes (recv first for a dot call).
static void emit_call(Comp* c, Node* n, const CCall* call, Node* recv, NodeList* args, Node* callee) {
    const int argc = args->count + (recv ? 1 : 0);
    if (needs_dispatch(call)) {
        ObjString* name = call->func ? call->func->name : str_cintern(c->prog->vm, "function");
        SType** sargs = pp_calloc((size_t)argc + 1, sizeof(SType*));
        for (int i = 0; i < argc; ++i) {
            sargs[i] = type_of_node(c, recv ? (i == 0 ? recv : args->items[i - 1]) : args->items[i]);
        }

        const int t = dispatch_table(c, call, name, sargs, argc, n->line);
        push_args(c, recv, args, NULL);
        op3(c, OP_DISPATCH, t, argc, n->line);

        return;
    }

    // a built-in operation on primitive values
    const int opc = call->op ? compare_opcode(call->op) : -1;
    if (opc >= 0) {
        const bool f = float_op(call->op);
        if (recv) {
            expr(c, recv);
            if (f && type_of_node(c, recv)->kind == ST_INT) {
                op0(c, OP_I2F);
            }
        }

        for (int i = 0; i < args->count; ++i) {
            expr(c, args->items[i]);
            if (f && type_of_node(c, args->items[i])->kind == ST_INT) {
                op0(c, OP_I2F);
            }
        }

        op0(c, (OpCode)opc);

        return;
    }

    if (call->target) {
        CInstance* t = call->target;
        if (is_nested(c, t->fn)) {
            char key[32];
            snprintf(key, sizeof key, "\x01fn%d", t->id);
            if (!load_var(c, str_cintern(c->prog->vm, key))) {
                fail(c, n->line, "the bytecode target cannot reach the nested function '%s' from here", t->fn->name->chars);
            }

            push_args(c, recv, args, t);
            op2(c, OP_CALL_VALUE, argc, n->line);

            return;
        }

        const BcFunc* f = func_for(c->prog, t);
        push_args(c, recv, args, t);
        op3(c, OP_CALL, f->index, argc, n->line);

        return;
    }

    CFunc* fn = call->func;
    if (fn && fn->kind == CF_CTOR) {
        push_args(c, recv, args, NULL);
        op3(c, OP_ENUM, enum_index(c->prog, fn->en), fn->variant, argc);

        return;
    }

    if (fn && fn->kind == CF_NATIVE) {
        if (fn->builtin && is_name_eq(fn->name, "lift") && argc == 1) {
            Node* a = recv ? recv : args->items[0];
            const SType* t = type_of_node(c, a);
            const char* bad = t->decl == c->prog->checker->option ? "none" : "fail";
            int variant = -1;
            for (int i = 0; t->kind == ST_ENUM && i < t->decl->nvariants; ++i) {
                if (str_equals(t->decl->variants[i].name, bad)) {
                    variant = i;
                }
            }

            if (variant < 0) {
                fail(c, n->line, "lift() of a value that is not an Option or an Error");
            }

            expr(c, a);
            op0(c, OP_DUP);
            op2(c, OP_IS_VARIANT, enum_index(c->prog, t->decl), variant);
            const int ok = jump(c, OP_JUMP_FALSE);
            emit_return(c);
            patch(c, ok);
            op1(c, OP_PAYLOAD, 0);

            return;
        }

        if (fn->builtin && is_name_eq(fn->name, "type_of")) {
            // not folded by the checker (a generic body): the static type of this instance
            const Node* a = recv ? recv : args->items[0];
            op1(c, OP_CONST, add_string(c->prog, st_name(type_of_node(c, (Node*)a))));

            return;
        }

        if (fn->builtin && (is_name_eq(fn->name, "[]") || is_name_eq(fn->name, "[]=")) && argc == (fn->name->len == 2 ? 2 : 3)) {
            push_args(c, recv, args, NULL);
            op1(c, fn->name->len == 2 ? OP_INDEX : OP_INDEX_SET, n->line);
            if (fn->name->len == 3) {
                op0(c, OP_VOID);
            }

            return;
        }

        const int k = native_of(c, fn, n->line);
        push_args(c, recv, args, NULL);
        op3(c, OP_CALL_NATIVE, k, argc, n->line);

        return;
    }

    // a value of a function type
    if (!callee) {
        fail(c, n->line, "internal: a call without a target");
    }

    const SType* ct = type_of_node(c, callee);
    if (ct->kind == ST_TYPE && ct->decl) {
        // Person({...}): the record, tagged with its type
        push_args(c, recv, args, NULL);
        op1(c, OP_TAG, typedef_index(c->prog, ct->decl));

        return;
    }

    expr(c, callee);
    for (int i = 0; recv && i < 1; ++i) {
        expr(c, recv);
    }

    SType* params = ct->kind == ST_FUNC ? (SType*)ct : NULL;
    for (int i = 0; i < args->count; ++i) {
        const int p = i + (recv ? 1 : 0);
        expr_to(c, args->items[i], params && !params->variadic && p < params->nargs ? params->args[p] : NULL);
    }

    op2(c, OP_CALL_VALUE, argc, n->line);
}



// ---------------------------------------------------------------- functions as values

// a nested function (declared in a function body, or a lambda) captures its surroundings
static bool is_nested(Comp* c, CFunc* f) {
    if (f->lambda) {
        return true;
    }

    const CScope* s = f->closure;
    if (!s || !s->inst) {
        return false;
    }

    return s->inst->fn != NULL; // declared in a function's body, not at a module's top level
}

// creates the closure of a compiled nested function: its captures come from c
static void make_closure(Comp* c, CInstance* inst, int line) {
    BcFunc* f = func_for(c->prog, inst);
    if (!f->compiled) {
        compile_function(c->prog, f, c, c->mod);
    }

    // the captures were found while compiling the body: the cells come from c's locals and captures
    for (int i = 0; i < f->ncaps; ++i) {
        op1(c, f->cap_local[i] ? OP_LOAD : OP_CAP, f->cap_index[i]);
    }

    op2(c, OP_CLOSURE, f->index, f->ncaps);
    (void)line;
}

// a type or a module used as a value (printed, compared): the runtime's object for it
static void type_value(Comp* c, Node* n, const SType* t) {
    VM* vm = c->prog->vm;
    if (t->kind == ST_MODULE) {
        if (t->module->rt) {
            op1(c, OP_CONST, add_const(c->prog, obj_val(t->module->rt)));

            return;
        }

        // one object per module, so `a == b` compares modules like the interpreter does
        BcModule* bm = module_of(c->prog, t->module);
        if (!IS_OBJ(bm->value)) {
            bm->value = obj_val(module_new(vm, t->module->name->chars, t->module->path, vm->builtins));
        }

        op1(c, OP_CONST, add_const(c->prog, bm->value));

        return;
    }

    TDecl* d = t->decl;
    if (!d) {
        fail(c, n->line, "the bytecode target cannot use this type as a value");
    }

    switch (d->kind) {
        case TD_ENUM: op1(c, OP_CONST, add_const(c->prog, obj_val(c->prog->enums[enum_index(c->prog, d)]))); return;
        case TD_ALIAS: op1(c, OP_CONST, add_const(c->prog, obj_val(c->prog->typedefs[typedef_index(c->prog, d)]))); return;
        case TD_NATIVE: {
            ObjNativeType* nt = d->which == NT_MAP ? vm->t_map : d->which == NT_SET ? vm->t_set : NULL;
            if (!nt) {
                const char* lib = module_lib_path(d->module);
                nt = lib ? vm_native_type(vm, lib, d->name->chars) : NULL;
            }

            if (!nt) {
                fail(c, n->line, "the bytecode target cannot find native type '%s'", d->name->chars);
            }

            op1(c, OP_CONST, add_const(c->prog, obj_val(nt)));

            return;
        }
        case TD_IFACE: {
            ObjInterface* it = (ObjInterface*)alloc_obj(vm, sizeof(ObjInterface), OBJ_INTERFACE);
            it->name = d->name;
            it->env = vm->builtins;
            op1(c, OP_CONST, add_const(c->prog, obj_val(it)));

            return;
        }
    }
}

static void native_value(Comp* c, CFuncSet* set, int line) {
    // a set of natives as a value (io:print passed as a callback): the runtime's overload set
    VM* vm = c->prog->vm;
    ObjOverload* ov = overload_new(vm, set->name);
    ov->obj.permanent = true;
    for (int i = 0; i < set->count; ++i) {
        CFunc* f = set->items[i];
        if (f->kind != CF_NATIVE || !f->native) {
            fail(c, line, "the bytecode target cannot use '%s' as a value", set->name->chars);
        }

        overload_add(ov, (Obj*)f->native);
    }

    op1(c, OP_CONST, add_const(c->prog, obj_val(ov)));
}

static void function_value(Comp* c, Node* n, SType* t) {
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

    if (users == 0) {
        native_value(c, set, n->line);

        return;
    }

    if (users != 1 || !only) {
        // several functions under one name: the calls through the value are resolved statically,
        // so the value itself only has to print as the function
        ObjOverload* ov = overload_new(c->prog->vm, set->name);
        op1(c, OP_CONST, add_const(c->prog, obj_val(ov)));

        return;
    }

    if (is_nested(c, only->fn)) {
        char key[32];
        snprintf(key, sizeof key, "\x01fn%d", only->id);
        if (!load_var(c, str_cintern(c->prog->vm, key))) {
            fail(c, n->line, "the bytecode target cannot reach the nested function '%s' from here", only->fn->name->chars);
        }

        return;
    }

    BcFunc* f = func_for(c->prog, only);
    op2(c, OP_CLOSURE, f->index, 0);
}



// ---------------------------------------------------------------- expressions

static void chain_at(Comp* c, Node* n, const ChainInfo* ci, int enc) {
    if (enc < 0) {
        expr(c, n->as.chain.operands.items[-enc - 1]);

        return;
    }

    const int k = enc;
    const char* op = n->as.chain.ops[k]->chars;
    if (strcmp(op, "&&") == 0 || strcmp(op, "||") == 0) {
        chain_at(c, n, ci, ci->left[k]);
        const int j = jump(c, op[0] == '&' ? OP_AND : OP_OR);
        chain_at(c, n, ci, ci->right[k]);
        patch(c, j);

        return;
    }

    const CCall* call = &ci->calls[k];
    if (needs_dispatch(call)) {
        chain_at(c, n, ci, ci->left[k]);
        chain_at(c, n, ci, ci->right[k]);
        SType* sargs[2] = {
            ci->left[k] < 0 ? type_of_node(c, n->as.chain.operands.items[-ci->left[k] - 1]) : ci->calls[ci->left[k]].type,
            ci->right[k] < 0 ? type_of_node(c, n->as.chain.operands.items[-ci->right[k] - 1]) : ci->calls[ci->right[k]].type,
        };
        op3(c, OP_DISPATCH, dispatch_table(c, call, n->as.chain.ops[k], sargs, 2, n->line), 2, n->line);

        return;
    }

    // the operands are subtrees of the chain, not nodes: evaluate them, then call with the stack
    const int opc = call->op ? compare_opcode(call->op) : -1;
    const bool f = opc >= 0 && float_op(call->op);
    const SType* lt = ci->left[k] < 0 ? type_of_node(c, n->as.chain.operands.items[-ci->left[k] - 1]) : ci->calls[ci->left[k]].type;
    const SType* rt = ci->right[k] < 0 ? type_of_node(c, n->as.chain.operands.items[-ci->right[k] - 1]) : ci->calls[ci->right[k]].type;
    chain_at(c, n, ci, ci->left[k]);
    if ((f || (call->target && call->target->nparams > 0 && call->target->params[0]->kind == ST_FLOAT)) && lt && lt->kind == ST_INT) {
        op0(c, OP_I2F);
    }

    chain_at(c, n, ci, ci->right[k]);
    if ((f || (call->target && call->target->nparams > 1 && call->target->params[1]->kind == ST_FLOAT)) && rt && rt->kind == ST_INT) {
        op0(c, OP_I2F);
    }

    if (opc >= 0) {
        op0(c, (OpCode)opc);

        return;
    }

    if (call->target) {
        if (is_nested(c, call->target->fn)) {
            fail(c, n->line, "an operator declared inside a function is not supported by the bytecode target yet");
        }

        op3(c, OP_CALL, func_for(c->prog, call->target)->index, 2, n->line);

        return;
    }

    if (call->func && call->func->kind == CF_NATIVE) {
        op3(c, OP_CALL_NATIVE, native_of(c, call->func, n->line), 2, n->line);

        return;
    }

    fail(c, n->line, "the bytecode target cannot compile operator '%s' here", op);
}

static void block_value(Comp* c, Node* block);
static void branchy(Comp* c, Node* n, bool value);

static void field(Comp* c, Node* n) {
    Node* obj = n->as.field.obj;
    const SType* ot = type_of_node(c, obj);
    ObjString* name = n->as.field.name;
    if (ot->kind == ST_MODULE) {
        CSym sym;
        if (!cmodule_export(c->prog->checker, ot->module, name, &sym)) {
            fail(c, n->line, "internal: module member '%s'", name->chars);
        }

        if (sym.kind == SYM_FUNCS) {
            function_value(c, n, type_of_node(c, n));

            return;
        }

        if (sym.kind == SYM_TYPE || sym.kind == SYM_MODULE) {
            type_value(c, n, type_of_node(c, n));

            return;
        }

        if (sym.kind != SYM_VAR) {
            fail(c, n->line, "the bytecode target cannot use '%s' as a value", name->chars);
        }

        if (ot->module->native) {
            Value v;
            if (!module_member(ot->module->rt, name, &v)) {
                fail(c, n->line, "internal: native module member '%s'", name->chars);
            }

            op1(c, OP_CONST, add_const(c->prog, v));

            return;
        }

        BcModule* m = module_of(c->prog, ot->module);
        ObjString* src = name;
        for (int i = 0; i < ot->module->nexports; ++i) {
            if (str_equals(ot->module->exp_names[i], name->chars)) {
                src = ot->module->exp_src[i]; // export SCALE -> factor
            }
        }

        const int g = global_of(m, src);
        if (g < 0) {
            fail(c, n->line, "the bytecode target cannot find '%s' in module '%s'", name->chars, ot->module->name->chars);
        }

        op1(c, OP_GLOAD, g);

        return;
    }

    if (ot->kind == ST_TYPE && ot->decl && ot->decl->kind == TD_ENUM) {
        const SType* t = type_of_node(c, n);
        if (t->kind == ST_ENUM) {
            for (int i = 0; i < ot->decl->nvariants; ++i) {
                if (ot->decl->variants[i].name == name || str_equals(ot->decl->variants[i].name, name->chars)) {
                    op3(c, OP_ENUM, enum_index(c->prog, ot->decl), i, 0);

                    return;
                }
            }
        }

        fail(c, n->line, "the bytecode target cannot use the constructor '%s' as a value yet", name->chars);
    }

    expr(c, obj);
    op2(c, OP_GET_FIELD, add_const(c->prog, obj_val(name)), n->line);
}

static void expr(Comp* c, Node* n) {
    switch (n->kind) {
        case N_INT:
            if (n->as.ival >= INT32_MIN && n->as.ival <= INT32_MAX) {
                op1(c, OP_INT, (int32_t)n->as.ival);
            } else {
                op1(c, OP_CONST, add_const(c->prog, int_val(n->as.ival)));
            }

            return;
        case N_FLOAT: op1(c, OP_CONST, add_const(c->prog, float_val(n->as.fval))); return;
        case N_STRING: op1(c, OP_CONST, add_const(c->prog, obj_val(n->as.str))); return;
        case N_BOOL: op0(c, n->as.bval ? OP_TRUE : OP_FALSE); return;
        case N_IDENT: {
            SType* t = type_of_node(c, n);
            if (t->kind == ST_OVERLOAD) {
                function_value(c, n, t);

                return;
            }

            if (t->kind == ST_TYPE || t->kind == ST_MODULE) {
                type_value(c, n, t);

                return;
            }

            if (!load_var(c, n->as.str)) {
                fail(c, n->line, "the bytecode target cannot find the variable '%s'", n->as.str->chars);
            }

            return;
        }
        case N_ARRAY: {
            const SType* t = type_of_node(c, n);
            for (int i = 0; i < n->as.array.items.count; ++i) {
                expr_to(c, n->as.array.items.items[i], t->elem);
            }

            op1(c, OP_ARRAY, n->as.array.items.count);

            return;
        }
        case N_OBJECT:
            op0(c, OP_OBJECT);
            for (int i = 0; i < n->as.object.values.count; ++i) {
                expr(c, n->as.object.values.items[i]);
                op1(c, OP_FIELD_INIT, add_const(c->prog, obj_val(n->as.object.keys[i])));
            }

            return;
        case N_CALL: emit_call(c, n, &info(c, n)->call, NULL, &n->as.call.args, n->as.call.callee); return;
        case N_METHOD: {
            Node* callee = n->as.method.callee;
            emit_call(c, n, &info(c, n)->call, n->as.method.recv, &n->as.method.args, callee);

            return;
        }
        case N_FIELD: field(c, n); return;
        case N_INDEX: {
            const CCall* call = &info(c, n)->call;
            if (call->target || (call->func && !(call->func->builtin && is_name_eq(call->func->name, "[]")))) {
                emit_call(c, n, call, n->as.index.obj, &n->as.index.args, NULL);

                return;
            }

            if (n->as.index.args.count != 1) {
                fail(c, n->line, "the bytecode target indexes built-in collections with one key");
            }

            expr(c, n->as.index.obj);
            expr(c, n->as.index.args.items[0]);
            op1(c, OP_INDEX, n->line);

            return;
        }
        case N_CHAIN: {
            const ChainInfo* ci = info(c, n)->chain;
            chain_at(c, n, ci, ci->root);

            return;
        }
        case N_FUNC: make_closure(c, info(c, n)->call.target, n->line); return;
        case N_IF:
        case N_MATCH: branchy(c, n, true); return;
        case N_BLOCK: block_value(c, n); return;
        case N_AS: {
            expr(c, n->as.as_.expr);
            const SType* t = type_of_node(c, n);
            op1(c, OP_AS, add_type(c->prog, t->nargs ? t->args[0] : st_prim(ST_UNKNOWN)));

            return;
        }
        case N_SCOPECALL: scope_call(c, n, true); return;
        default: fail(c, n->line, "the bytecode target cannot compile this expression (kind %d)", n->kind);
    }
}



// ---------------------------------------------------------------- patterns

typedef struct {
    int* list;
    int count;
    int cap;
} Fails;

static CFunc* ctor_named(Comp* c, ObjString* name) {
    // the checker resolved `some` / `none` in a pattern by scope; find the constructor among the enums
    const CScope* s = c->inst->fn && c->inst->fn->closure ? c->inst->fn->closure : c->mod->module->scope;
    const CSym* sym = scope_lookup((CScope*)s, name);
    if (!sym) {
        sym = scope_lookup(c->prog->checker->builtins, name);
    }

    for (int i = 0; sym && sym->kind == SYM_FUNCS && i < sym->funcs->count; ++i) {
        if (sym->funcs->items[i]->kind == CF_CTOR) {
            return sym->funcs->items[i];
        }
    }

    return NULL;
}

// tests the value in `slot` against p, jumping to one of fails when it does not match; binds names
static void pattern(Comp* c, Pattern* p, int slot, Fails* fails) {
    switch (p->kind) {
        case P_WILD: return;
        case P_LITERAL:
            op1(c, OP_LOAD, slot);
            expr(c, p->lit);
            op0(c, OP_VALUES_EQ);
            add_patch(&fails->list, &fails->count, &fails->cap, jump(c, OP_JUMP_FALSE));

            return;
        case P_BIND: {
            CFunc* unit = ctor_named(c, p->name);
            if (unit && unit->en->variants[unit->variant].npayload == 0) {
                op1(c, OP_LOAD, slot);
                op2(c, OP_IS_VARIANT, enum_index(c->prog, unit->en), unit->variant);
                add_patch(&fails->list, &fails->count, &fails->cap, jump(c, OP_JUMP_FALSE));

                return;
            }

            op1(c, OP_LOAD, slot);
            declare_store(c, p->name);

            return;
        }
        case P_VARIANT: {
            Node* cn = p->ctor;
            if (cn->kind == N_IDENT && str_equals(cn->as.str, "tuple")) {
                op1(c, OP_LOAD, slot);
                op1(c, OP_IS_TUPLE, p->nsubs);
                add_patch(&fails->list, &fails->count, &fails->cap, jump(c, OP_JUMP_FALSE));
                for (int i = 0; i < p->nsubs; ++i) {
                    const int s = new_slot(c);
                    op1(c, OP_LOAD, slot);
                    op1(c, OP_TUPLE_GET, i);
                    op1(c, OP_STORE, s);
                    pattern(c, p->subs[i], s, fails);
                }

                return;
            }

            TDecl* en = NULL;
            int variant = -1;
            if (cn->kind == N_IDENT) {
                CFunc* f = ctor_named(c, cn->as.str);
                if (f) {
                    en = f->en;
                    variant = f->variant;
                }
            } else if (cn->kind == N_FIELD) {
                const SType* ot = type_of_node(c, cn->as.field.obj);
                if (ot->kind == ST_TYPE && ot->decl && ot->decl->kind == TD_ENUM) {
                    en = ot->decl;
                    for (int i = 0; i < en->nvariants; ++i) {
                        if (str_equals(en->variants[i].name, cn->as.field.name->chars)) {
                            variant = i;
                        }
                    }
                }
            }

            if (!en || variant < 0) {
                fail(c, p->line, "the bytecode target does not understand this pattern");
            }

            op1(c, OP_LOAD, slot);
            op2(c, OP_IS_VARIANT, enum_index(c->prog, en), variant);
            add_patch(&fails->list, &fails->count, &fails->cap, jump(c, OP_JUMP_FALSE));
            for (int i = 0; i < p->nsubs; ++i) {
                const int s = new_slot(c);
                op1(c, OP_LOAD, slot);
                op1(c, OP_PAYLOAD, i);
                op1(c, OP_STORE, s);
                pattern(c, p->subs[i], s, fails);
            }

            return;
        }
    }
}

static void patch_fails(Comp* c, Fails* fails) {
    for (int i = 0; i < fails->count; ++i) {
        patch(c, fails->list[i]);
    }

    free(fails->list);
    fails->list = NULL;
    fails->count = fails->cap = 0;
}



// ---------------------------------------------------------------- if / match / blocks

static void block_stmts(Comp* c, Node* block, bool value) {
    const int count = block->as.block.stmts.count;
    for (int i = 0; i < count; ++i) {
        Node* s = block->as.block.stmts.items[i];
        if (value && i == count - 1) {
            op1(c, OP_LINE, s->line);
            if (s->kind == S_EXPR && s->as.expr.yield) {
                Node* e = s->as.expr.expr;
                if (e->kind == N_IF || e->kind == N_MATCH) {
                    branchy(c, e, true);
                } else {
                    expr(c, e);
                }
            } else {
                stmt(c, s);
                op0(c, OP_VOID);
            }

            continue;
        }

        stmt(c, s);
    }

    if (value && count == 0) {
        op0(c, OP_VOID);
    }
}

static void block_value(Comp* c, Node* block) {
    begin_scope(c);
    block_stmts(c, block, true);
    end_scope(c);
}

static void block_or_value(Comp* c, Node* block, bool value) {
    begin_scope(c);
    block_stmts(c, block, value);
    end_scope(c);
}

// if / match; value: leaves the value of the branch taken
static void branchy(Comp* c, Node* n, bool value) {
    if (n->kind == N_IF) {
        begin_scope(c);
        Fails fails = {0};
        if (n->as.if_.pat) {
            const int s = new_slot(c);
            expr(c, n->as.if_.cond);
            op1(c, OP_STORE, s);
            pattern(c, n->as.if_.pat, s, &fails);
        } else {
            expr(c, n->as.if_.cond);
            add_patch(&fails.list, &fails.count, &fails.cap, jump(c, OP_JUMP_FALSE));
        }

        block_or_value(c, n->as.if_.then_b, value);
        end_scope(c);
        const int end = jump(c, OP_JUMP);
        patch_fails(c, &fails);
        if (n->as.if_.else_b) {
            Node* e = n->as.if_.else_b;
            if (e->kind == N_IF || e->kind == N_MATCH) {
                branchy(c, e, value);
            } else {
                block_or_value(c, e, value);
            }
        } else if (value) {
            op0(c, OP_VOID);
        }

        patch(c, end);

        return;
    }

    begin_scope(c);
    const int subject = new_slot(c);
    expr(c, n->as.match.subject);
    op1(c, OP_STORE, subject);
    int* ends = NULL;
    int nends = 0;
    int capends = 0;
    for (int i = 0; i < n->as.match.narms; ++i) {
        MatchArm* arm = &n->as.match.arms[i];
        begin_scope(c);
        Fails fails = {0};
        pattern(c, arm->pat, subject, &fails);
        if (arm->guard) {
            expr(c, arm->guard);
            add_patch(&fails.list, &fails.count, &fails.cap, jump(c, OP_JUMP_FALSE));
        }

        if (arm->body_is_block) {
            block_or_value(c, arm->body, value);
        } else {
            expr(c, arm->body);
            if (!value) {
                op0(c, OP_POP);
            }
        }

        end_scope(c);
        add_patch(&ends, &nends, &capends, jump(c, OP_JUMP));
        patch_fails(c, &fails);
    }

    op1(c, OP_LOAD, subject);
    op1(c, OP_NO_MATCH, n->line);
    for (int i = 0; i < nends; ++i) {
        patch(c, ends[i]);
    }

    free(ends);
    end_scope(c);
}



// ---------------------------------------------------------------- statements

static void let(Comp* c, Node* n) {
    const NodeInfo* li = info(c, n->as.let.init ? n->as.let.init : n);
    SType* declared = NULL;
    if (n->as.let.type) {
        // the variable's type is the declared one; the checker gave the initialiser's
        Ctx x = {0};
        x.c = c->prog->checker;
        declared = resolve_type(&x, n->as.let.type, c->inst->fn && c->inst->fn->closure ? c->inst->fn->closure : c->mod->module->scope, &c->inst->bindings);
    }

    if (n->as.let.init) {
        expr(c, n->as.let.init);
        convert(c, li ? li->type : NULL, declared);
        tag_as(c, declared);
    } else {
        op0(c, n->as.let.is_const ? OP_TRUE : OP_VOID);
    }

    declare_store(c, n->as.let.name);
}

static void assign(Comp* c, Node* n) {
    Node* target = n->as.assign.target;
    Node* value = n->as.assign.value;
    const NodeInfo* ni = info(c, n);
    const SType* tt = target->kind == N_INDEX && !n->as.assign.op ? NULL : type_of_node(c, target);
    if (!n->as.assign.op) {
        if (target->kind == N_IDENT) {
            expr_to(c, value, tt);
            store_var(c, target->as.str, n->line);

            return;
        }

        if (target->kind == N_FIELD) {
            expr(c, target->as.field.obj);
            expr_to(c, value, tt);
            op2(c, OP_SET_FIELD, add_const(c->prog, obj_val(target->as.field.name)), n->line);

            return;
        }

        // c[k] = v
        const CCall* call = &ni->call;
        if (call->target || (call->func && !(call->func->builtin && is_name_eq(call->func->name, "[]=")))) {
            NodeList args = {0};
            for (int i = 0; i < target->as.index.args.count; ++i) {
                nl_push_raw(&args, target->as.index.args.items[i]);
            }

            nl_push_raw(&args, value);
            emit_call(c, n, call, target->as.index.obj, &args, NULL);
            op0(c, OP_POP);
            free(args.items);

            return;
        }

        expr(c, target->as.index.obj);
        expr(c, target->as.index.args.items[0]);
        const SType* ot = type_of_node(c, target->as.index.obj);
        expr_to(c, value, ot->kind == ST_ARRAY ? ot->elem : NULL);
        op1(c, OP_INDEX_SET, n->line);

        return;
    }

    // target op= value: in place (the op= function), else target = target op value
    const CCall* call = &ni->call;
    const bool in_place = call->func && (call->func->name == n->as.assign.opeq || str_equals(call->func->name, n->as.assign.opeq->chars));
    const SType* vt = type_of_node(c, value);
    if (in_place) {
        NodeList args = {0};
        nl_push_raw(&args, value);
        emit_call(c, n, call, target, &args, NULL);
        op0(c, OP_POP);
        free(args.items);

        return;
    }

    // evaluate the target's current value once
    int obj_slot = -1;
    int key_slot = -1;
    const int nkeys = target->kind == N_INDEX ? target->as.index.args.count : 0;
    if (target->kind == N_FIELD) {
        obj_slot = new_slot(c);
        expr(c, target->as.field.obj);
        op1(c, OP_STORE, obj_slot);
        op1(c, OP_LOAD, obj_slot);
        op2(c, OP_GET_FIELD, add_const(c->prog, obj_val(target->as.field.name)), n->line);
    } else if (target->kind == N_INDEX) {
        obj_slot = new_slot(c);
        key_slot = new_slot(c);
        for (int i = 1; i < nkeys; ++i) {
            new_slot(c);
        }

        expr(c, target->as.index.obj);
        op1(c, OP_STORE, obj_slot);
        for (int i = 0; i < nkeys; ++i) {
            expr(c, target->as.index.args.items[i]);
            op1(c, OP_STORE, key_slot + i);
        }

        const CCall* get = &info(c, target)->call;
        op1(c, OP_LOAD, obj_slot);
        for (int i = 0; i < nkeys; ++i) {
            op1(c, OP_LOAD, key_slot + i);
        }

        if (get->target) {
            op3(c, OP_CALL, func_for(c->prog, get->target)->index, nkeys + 1, n->line);
        } else if (nkeys == 1) {
            op1(c, OP_INDEX, n->line);
        } else {
            fail(c, n->line, "the bytecode target indexes built-in collections with one key");
        }
    } else {
        expr(c, target);
    }

    const int opc = call->op ? compare_opcode(call->op) : -1;
    const bool f = opc >= 0 && float_op(call->op);
    if (f && tt->kind == ST_INT) {
        op0(c, OP_I2F);
    }

    expr(c, value);
    if (f && vt->kind == ST_INT) {
        op0(c, OP_I2F);
    }

    if (opc >= 0) {
        op0(c, (OpCode)opc);
    } else if (call->target) {
        op3(c, OP_CALL, func_for(c->prog, call->target)->index, 2, n->line);
    } else if (call->func && call->func->kind == CF_NATIVE) {
        op3(c, OP_CALL_NATIVE, native_of(c, call->func, n->line), 2, n->line);
    } else {
        fail(c, n->line, "the bytecode target cannot compile '%s' here", n->as.assign.opeq->chars);
    }

    // the result goes back where the value came from
    if (target->kind == N_IDENT) {
        store_var(c, target->as.str, n->line);
    } else if (target->kind == N_FIELD) {
        const int r = new_slot(c);
        op1(c, OP_STORE, r);
        op1(c, OP_LOAD, obj_slot);
        op1(c, OP_LOAD, r);
        op2(c, OP_SET_FIELD, add_const(c->prog, obj_val(target->as.field.name)), n->line);
    } else {
        const int r = new_slot(c);
        op1(c, OP_STORE, r);
        op1(c, OP_LOAD, obj_slot);
        for (int i = 0; i < nkeys; ++i) {
            op1(c, OP_LOAD, key_slot + i);
        }

        op1(c, OP_LOAD, r);
        const CCall* store = &ni->call2;
        if (store->target) {
            op3(c, OP_CALL, func_for(c->prog, store->target)->index, nkeys + 2, n->line);
            op0(c, OP_POP);
        } else {
            op1(c, OP_INDEX_SET, n->line);
        }
    }
}

static void loop_begin(Comp* c, BcLoop* l) {
    memset(l, 0, sizeof *l);
    l->prev = c->loop;
    c->loop = l;
}

static void loop_end(Comp* c, BcLoop* l, int cont_target) {
    for (int i = 0; i < l->nconts; ++i) {
        c->fn->code[l->conts[i]] = cont_target;
    }

    for (int i = 0; i < l->nbreaks; ++i) {
        patch(c, l->breaks[i]);
    }

    free(l->breaks);
    free(l->conts);
    c->loop = l->prev;
}

static void for_loop(Comp* c, Node* n) {
    begin_scope(c);
    const int it = new_slot(c);
    new_slot(c); // the position
    new_slot(c); // the index
    expr(c, n->as.for_.iter);
    if (n->as.for_.proto) {
        // a value of a type that implements Iterable / KeyIterable: unless it turns out to be a built-in
        // collection or a generator (iterated directly), the prelude's iterate($for) makes a generator of it
        const int direct = jump(c, OP_JUMP_ITERABLE);
        begin_scope(c);
        const BcLocal* v = add_local(c, str_cintern(c->prog->vm, "$for"));
        op1(c, OP_STORE, v->slot);
        expr(c, n->as.for_.proto);
        end_scope(c);
        patch(c, direct);
    }

    op1(c, OP_ITER_INIT, it);
    BcLoop l;
    loop_begin(c, &l);
    const int top = c->fn->count;
    begin_scope(c);
    // the loop variables: fresh in every iteration (a closure keeps the one of its iteration)
    const BcLocal* a = add_local(c, n->as.for_.var1);
    const BcLocal* b = n->as.for_.var2 ? add_local(c, n->as.for_.var2) : NULL;
    emit(c, OP_ITER_NEXT);
    emit(c, it);
    emit(c, a->slot);
    emit(c, b ? b->slot : -1);
    emit(c, -1);
    const int exit = c->fn->count - 1;
    if (a->boxed) {
        op1(c, OP_BOX, a->slot);
    }

    if (b && b->boxed) {
        op1(c, OP_BOX, b->slot);
    }

    block_or_value(c, n->as.for_.body, false);
    end_scope(c);
    op1(c, OP_JUMP, top);
    patch(c, exit);
    loop_end(c, &l, top);
    end_scope(c);
}

// return: in the body of a scope call's block it leaves the scope call (and then the function
// around it) instead; the value is on top
static void emit_return(Comp* c) {
    if (c->fn->kind == BC_BLOCK || c->fn->kind == BC_CALLBACK) {
        op1(c, OP_SCOPE_EXIT, SCOPE_RETURN);

        return;
    }

    op0(c, OP_RETURN);
}

// break / continue: a jump in the loop, or, from a block without a loop, an exit of the scope call
// that the loop around the call continues
static void emit_leave_loop(Comp* c, bool cont) {
    BcLoop* l = c->loop;
    if (l) {
        if (cont) {
            add_patch(&l->conts, &l->nconts, &l->capconts, jump(c, OP_JUMP));
        } else {
            add_patch(&l->breaks, &l->nbreaks, &l->capbreaks, jump(c, OP_JUMP));
        }

        return;
    }

    if (c->fn->kind == BC_BLOCK || c->fn->kind == BC_CALLBACK) {
        op1(c, OP_SCOPE_EXIT, cont ? SCOPE_CONTINUE : SCOPE_BREAK);

        return;
    }

    op1(c, OP_FAIL, add_string(c->prog, cont ? "'continue' used outside of a loop" : "'break' used outside of a loop"));
}

// copies the captures compiling a body found into its function
static void store_captures(BcFunc* f, const Comp* c) {
    f->ncaps = c->ncaps;
    f->cap_local = pp_calloc((size_t)c->ncaps + 1, sizeof(bool));
    f->cap_index = pp_calloc((size_t)c->ncaps + 1, sizeof(int));
    for (int i = 0; i < c->ncaps; ++i) {
        f->cap_local[i] = c->caps[i].from_local;
        f->cap_index[i] = c->caps[i].index;
    }
}

// The body of a scope call's block (or of a defer) as a closure of c, left on the stack. The
// checker typed it as part of c's instance; it captures c's variables like a lambda. A block
// takes `scope` (slot 0) and gives what it yields; a defer takes and gives nothing.
static void inline_closure(Comp* c, Node* body, BcKind kind, int line) {
    BcProgram* prog = c->prog;
    BcFunc* f = pp_calloc(1, sizeof(BcFunc));
    f->index = prog->nfuncs;
    GROW(prog->funcs, prog->nfuncs, prog->capfuncs);
    prog->funcs[prog->nfuncs++] = f;
    f->kind = kind;
    f->scope = c->fn->kind == BC_FUNCTION || kind == BC_DEFER ? c->fn->name : c->fn->scope;
    f->name = kind == BC_DEFER ? str_cintern(prog->vm, "defer") : f->scope;
    f->file = c->fn->file;
    f->line = line;
    f->nparams = kind == BC_DEFER ? 0 : 1;
    f->compiled = true;

    Comp b = {0};
    b.prog = prog;
    b.fn = f;
    b.inst = c->inst;
    b.outer = c;
    b.mod = c->mod;
    b.ret = c->ret;
    names_used_nested(&b.boxed, body, false);
    if (kind != BC_DEFER) {
        const BcLocal* l = add_local(&b, prog->checker->s_scope);
        if (l->boxed) {
            op1(&b, OP_BOX, l->slot);
        }
    }

    begin_scope(&b);
    block_stmts(&b, body, kind != BC_DEFER);
    end_scope(&b);
    if (kind == BC_DEFER) {
        op0(&b, OP_VOID);
    }

    op0(&b, OP_RETURN);
    store_captures(f, &b);
    free(b.locals);
    free(b.caps);
    free(b.boxed.items);
    for (int i = 0; i < f->ncaps; ++i) {
        op1(c, f->cap_local[i] ? OP_LOAD : OP_CAP, f->cap_index[i]);
    }

    op2(c, OP_CLOSURE, f->index, f->ncaps);
}

// _name args { body }: calls the scope function with the block (a closure of the body) and the
// args; a `const f = x` arg is also a local of the body. The call ends with how the block left:
// normally (the scope function's result is the value, left when `value`), or early with
// return / break / continue (a failing lift is a return), which then happens here.
static void scope_call(Comp* c, Node* n, bool value) {
    const CCall* call = &info(c, n)->call;
    CInstance* t = call->target;
    if (!t || needs_dispatch(call) || t->nparams < 1) {
        fail(c, n->line, "the bytecode target calls a scope only when the checker chose it");
    }

    begin_scope(c);
    int func = -1;
    if (is_nested(c, t->fn)) {
        char key[32];
        snprintf(key, sizeof key, "\x01fn%d", t->id);
        if (!load_var(c, str_cintern(c->prog->vm, key))) {
            fail(c, n->line, "the bytecode target cannot reach the nested scope '%s' from here", t->fn->name->chars);
        }
    } else {
        func = func_for(c->prog, t)->index;
        op0(c, OP_VOID);
    }

    op0(c, OP_VOID); // the value of the call, or of an early return
    op0(c, OP_VOID); // the current defer block
    // the args first, in order, into locals: the block captures the `const f = x` ones
    NodeList* al = &n->as.scall.args;
    int* slots = pp_calloc((size_t)al->count + 1, sizeof(int));
    bool* boxed = pp_calloc((size_t)al->count + 1, sizeof(bool));
    for (int i = 0; i < al->count; ++i) {
        Node* a = al->items[i];
        const SType* pt = i + 1 < t->nparams ? t->params[i + 1] : NULL;
        if (a->kind == S_LET) {
            expr_to(c, a->as.let.init, pt);
            const BcLocal* l = add_local(c, a->as.let.name);
            slots[i] = l->slot;
            boxed[i] = l->boxed;
            op1(c, OP_STORE, l->slot);
            if (l->boxed) {
                op1(c, OP_BOX, l->slot);
            }
        } else {
            expr_to(c, a, pt);
            slots[i] = new_slot(c);
            op1(c, OP_STORE, slots[i]);
        }
    }

    const SType* bt = t->params[0];
    inline_closure(c, n->as.scall.body, bt->kind == ST_BLOCK && bt->variadic ? BC_CALLBACK : BC_BLOCK, n->line);
    BcFunc* bf = c->prog->funcs[c->prog->nfuncs - 1];
    bf->scope = n->as.scall.name;
    bf->name = n->as.scall.name;
    for (int i = 0; i < al->count; ++i) {
        op1(c, OP_LOAD, slots[i]);
        if (boxed[i]) {
            op0(c, OP_CELL_GET);
        }
    }

    free(slots);
    free(boxed);
    op3(c, OP_SCOPE_CALL, func, al->count + 1, n->line);
    end_scope(c);

    // value exit: continue with how the call ended
    op0(c, OP_DUP);
    op1(c, OP_INT, SCOPE_NORMAL);
    op0(c, OP_EQ_I);
    const int normal = jump(c, OP_JUMP_TRUE);
    op0(c, OP_DUP);
    op1(c, OP_INT, SCOPE_RETURN);
    op0(c, OP_EQ_I);
    const int not_return = jump(c, OP_JUMP_FALSE);
    op0(c, OP_POP);
    emit_return(c);
    patch(c, not_return);
    op1(c, OP_INT, SCOPE_BREAK);
    op0(c, OP_EQ_I);
    const int not_break = jump(c, OP_JUMP_FALSE);
    op0(c, OP_POP);
    emit_leave_loop(c, false);
    patch(c, not_break);
    op0(c, OP_POP);
    emit_leave_loop(c, true);
    patch(c, normal);
    op0(c, OP_POP);
    if (!value) {
        op0(c, OP_POP);
    }
}

static void declare_nested(Comp* c, Node* n) {
    const NodeInfo* ni = info(c, n);
    CFunc* f = ni ? ni->call.func : NULL;
    if (!f || f->kind != CF_USER || !is_nested(c, f)) {
        return; // top level functions are compiled when called
    }

    // each instance becomes a closure in a cell named after it, so bodies (its own too) can call it;
    // a declaration without a body makes the cell, the definition fills it
    for (int i = 0; i < f->ninsts; ++i) {
        CInstance* inst = f->insts[i];
        char key[32];
        snprintf(key, sizeof key, "\x01fn%d", inst->id);
        ObjString* name = str_cintern(c->prog->vm, key);
        if (!find_local(c, name)) {
            names_add(&c->boxed, name);
            op0(c, OP_VOID);
            declare_store(c, name);
        }

        if (n->as.func.body) {
            make_closure(c, inst, n->line);
            store_var(c, name, n->line);
        }
    }
}

static void stmt(Comp* c, Node* n) {
    op1(c, OP_LINE, n->line);
    switch (n->kind) {
        case S_EXPR: {
            Node* e = n->as.expr.expr;
            if (e->kind == N_IF || e->kind == N_MATCH) {
                branchy(c, e, false);

                return;
            }

            if (e->kind == N_SCOPECALL) {
                scope_call(c, e, false);

                return;
            }

            expr(c, e);
            op0(c, OP_POP);

            return;
        }
        case S_LET: let(c, n); return;
        case S_ASSIGN: assign(c, n); return;
        case S_FUNCDECL: declare_nested(c, n); return;
        case S_TYPE:
        case S_INTERFACE:
        case S_IMPL:
        case S_PRECEDENCE: return;
        case S_ENUM: {
            const CSym* sym = scope_lookup(c->mod->module->scope, n->as.enum_decl.name);
            if (sym && sym->kind == SYM_TYPE && sym->tdecl) {
                enum_values(c, n, sym->tdecl);
            }

            return;
        }
        case S_IMPORT: {
            Ctx x = {0};
            x.c = c->prog->checker;
            const char* saved = x.c->file;
            x.c->file = c->mod->module->path;
            CModule* m = load_module(&x, n, n->line);
            x.c->file = saved;
            BcModule* bm = module_of(c->prog, m);
            if (!m->native) {
                op1(c, OP_RUN_MODULE, (int)(bm - c->prog->modules));
            }

            if (n->as.import.destructure && !m->native) {
                for (int i = 0; i < n->as.import.nnames; ++i) {
                    ObjString* src = n->as.import.names[i];
                    for (int k = 0; k < m->nexports; ++k) {
                        if (str_equals(m->exp_names[k], src->chars)) {
                            src = m->exp_src[k];
                        }
                    }

                    const int g = global_of(bm, src);
                    if (g >= 0) {
                        add_global_name(c->mod, n->as.import.names[i], g);
                    }
                }
            }

            return;
        }
        case S_EXPORT:
            if (n->as.export_.decl) {
                stmt(c, n->as.export_.decl);
            }

            return;
        case S_FOR: for_loop(c, n); return;
        case S_WHILE: {
            BcLoop l;
            loop_begin(c, &l);
            const int top = c->fn->count;
            expr(c, n->as.while_.cond);
            const int exit = jump(c, OP_JUMP_FALSE);
            block_or_value(c, n->as.while_.body, false);
            op1(c, OP_JUMP, top);
            patch(c, exit);
            loop_end(c, &l, top);

            return;
        }
        case S_BREAK: emit_leave_loop(c, false); return;
        case S_CONTINUE: emit_leave_loop(c, true); return;
        case S_RETURN:
            if (c->fn->generator) {
                // emit value: pause with it; return value: it is the last one; return: the end
                SType* elem = c->ret && c->ret->nargs ? c->ret->args[0] : NULL;
                if (n->as.ret.value) {
                    expr_to(c, n->as.ret.value, elem);
                    tag_as(c, elem);
                }

                if (n->as.ret.emit) {
                    op0(c, OP_SUSPEND);
                } else {
                    op1(c, OP_GEN_END, n->as.ret.value ? 1 : 0);
                }

                return;
            }

            if (n->as.ret.value) {
                expr_to(c, n->as.ret.value, c->ret);
                tag_as(c, c->ret);
            } else {
                op0(c, OP_VOID);
            }

            emit_return(c);

            return;
        case N_BLOCK: block_or_value(c, n, false); return;
        case S_DEFER:
            // the cleanup of the running scope call: a closure of the scope function's body
            inline_closure(c, n->as.defer.body, BC_DEFER, n->line);
            op0(c, OP_DEFER);

            return;
        default:
            expr(c, n);
            op0(c, OP_POP);

            return;
    }
}



// ---------------------------------------------------------------- functions and modules

static void compile_function(BcProgram* prog, BcFunc* f, Comp* outer, BcModule* mod) {
    f->compiled = true;
    CInstance* inst = f->inst;
    CFunc* fn = inst->fn;
    if (inst->dyn) {
        Comp tmp = {0};
        tmp.prog = prog;
        tmp.fn = f;
        fail(&tmp, inst->dyn_line, "'%s' is not statically typed (%s); compile it with --target=interp",
             fn && fn->name ? fn->name->chars : "the top level", inst->dyn_reason ? inst->dyn_reason : "dynamic");
    }

    Comp c = {0};
    c.prog = prog;
    c.fn = f;
    c.inst = inst;
    c.outer = outer;
    c.mod = fn && fn->module ? module_of(prog, fn->module) : mod;
    c.ret = inst->ret;
    if (fn) {
        names_used_nested(&c.boxed, fn->body, false);
        // parameters: slots 0 .. n-1
        for (int i = 0; i < inst->nparams; ++i) {
            BcLocal* l = add_local(&c, fn->sig && i < fn->sig->nparams && fn->sig->params[i].name ? fn->sig->params[i].name
                                                                                              : str_cintern(prog->vm, "\x01p"));
            if (is_nominal(inst->params[i]) || (inst->params[i]->kind == ST_ARRAY && is_nominal(inst->params[i]->elem))) {
                op1(&c, OP_LOAD, l->slot);
                tag_as(&c, inst->params[i]);
                op0(&c, OP_POP);
            }

            if (l->boxed) {
                op1(&c, OP_BOX, l->slot);
            }
        }

        // an expression-bodied lambda gives its expression (the body ends with `yield`)
        const bool implicit = fn->sig && fn->sig->implicit_result;
        begin_scope(&c);
        block_stmts(&c, fn->body, implicit);
        end_scope(&c);
        if (f->generator) {
            op1(&c, OP_GEN_END, 0);
        } else {
            if (!implicit) {
                op0(&c, OP_VOID);
            }

            op0(&c, OP_RETURN);
        }
    }

    store_captures(f, &c);

    free(c.locals);
    free(c.caps);
    free(c.boxed.items);
}

static BcModule* module_of(BcProgram* prog, CModule* m) {
    for (int i = 0; i < prog->nmodules; ++i) {
        if (prog->modules[i].module == m) {
            return &prog->modules[i];
        }
    }

    // modules are few; their array is allocated once for all of the checker's modules
    BcModule* bm = &prog->modules[prog->nmodules++];
    memset(bm, 0, sizeof *bm);
    bm->module = m;
    if (m->top && !m->native) {
        bm->init = func_for(prog, m->top);
        bm->init->name = str_cintern(prog->vm, "<top level>");
        bm->init->is_init = true;
        bm->init->file = m->path;
        Comp c = {0};
        c.prog = prog;
        c.fn = bm->init;
        c.inst = m->top;
        c.mod = bm;
        c.module_top = true;
        c.ret = st_prim(ST_UNKNOWN);
        bm->init->compiled = true;
        Node* program = m->program;
        if (!program && m == prog->checker->main) {
            program = NULL;
        }

        if (program) {
            names_used_nested(&c.boxed, program, false);
            for (int i = 0; i < program->as.block.stmts.count; ++i) {
                stmt(&c, program->as.block.stmts.items[i]);
            }
        }

        op0(&c, OP_VOID);
        op0(&c, OP_RETURN);
        free(c.locals);
        free(c.caps);
        free(c.boxed.items);
    }

    return bm;
}

BcProgram* bc_compile(VM* vm, Checker* ch) {
    BcProgram* prog = pp_calloc(1, sizeof(BcProgram));
    prog->vm = vm;
    prog->checker = ch;
    prog->roots = array_new(vm, 64);
    vm->bc_roots = obj_val(prog->roots);
    prog->modules = pp_calloc((size_t)ch->nmodules + 4, sizeof(BcModule));
    jmp_buf jb;
    prog->err = &jb;
    if (setjmp(jb)) {
        return prog;
    }

    if (setjmp(ch->jb)) {
        // instantiating a function for dynamic dispatch found an error (already printed)
        prog->error = "the program cannot be compiled";

        return prog;
    }

    module_of(prog, ch->main);
    // the functions called from compiled code, compiled until none is left
    for (bool more = true; more;) {
        more = false;
        for (int i = 0; i < prog->nfuncs; ++i) {
            BcFunc* f = prog->funcs[i];
            if (!f->compiled) {
                CModule* m = f->inst->fn && f->inst->fn->module ? f->inst->fn->module : ch->main;
                compile_function(prog, f, NULL, module_of(prog, m));
                more = true;
            }
        }
    }

    return prog;
}



// ---------------------------------------------------------------- dump

void bc_dump(BcProgram* prog, FILE* out) {
    for (int i = 0; i < prog->nfuncs; ++i) {
        const BcFunc* f = prog->funcs[i];
        fprintf(out, "function %d %s (%d params, %d locals, %d captures)\n", f->index, f->name ? f->name->chars : "?",
                f->nparams, f->nlocals, f->ncaps);
        for (int ip = 0; ip < f->count;) {
            const int op = f->code[ip];
            fprintf(out, "  %5d  %-12s", ip, op >= 0 && op < OP_COUNT && BC_OPS[op].name ? BC_OPS[op].name : "?");
            const int n = op >= 0 && op < OP_COUNT ? BC_OPS[op].noperands : 0;
            for (int k = 1; k <= n; ++k) {
                fprintf(out, " %d", f->code[ip + k]);
            }

            if (op == OP_CONST) {
                StrBuf sb;
                sb_init(&sb);
                value_format(&sb, prog->consts[f->code[ip + 1]], false, 0);
                fprintf(out, "    ; %.40s", sb.data ? sb.data : "");
                sb_free(&sb);
            } else if (op == OP_CALL_NATIVE) {
                fprintf(out, "    ; %s", prog->natives[f->code[ip + 1]].name);
            } else if (op == OP_CALL) {
                const BcFunc* t = prog->funcs[f->code[ip + 1]];
                fprintf(out, "    ; %s", t->name ? t->name->chars : "?");
            }

            fputc('\n', out);
            ip += 1 + n;
        }
    }
}

int bytecode_run(VM* vm, Checker* c, bool dump) {
    BcProgram* prog = bc_compile(vm, c);
    if (prog->error) {
        fprintf(stderr, "%s:%d: error: %s\n", prog->error_file ? prog->error_file : "?", prog->error_line, prog->error);

        return 70;
    }

    if (dump) {
        bc_dump(prog, stdout);
    }

    return bc_run(prog);
}
