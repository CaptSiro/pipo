#include <math.h>

#include "bytecode.h"



// The bytecode VM: runs what bytecode.c compiled. See bytecode.h for the layout of the stack.

typedef struct {
    BcFunc* fn;
    ObjClosure* closure;
    int base;            // vm->stack index of local slot 0
    int ip;
    bool pop_callee;     // called through CALL_VALUE: the callee sits below the arguments
    bool traced;         // has an entry in vm->frames
    const char* saved_file;
    ObjGenerator* gen;   // the frame runs a generator's body (resumed by next())
} BcFrame;

// a running scope call (OP_SCOPE_CALL): a block that leaves early unwinds to it
typedef struct BcScope {
    struct BcScope* prev;
    jmp_buf jb;
    int slot;            // vm->stack index of the call's value; its defer block is at slot + 1
    int nframes;
    int fp;
    jmp_buf* err_jmp;
    bool gc_disabled;
    const char* file;
    ScopeExit exit;
} BcScope;

static BcProgram* prog;
static BcFrame* frames;
static int nframes;
static int capframes;
static BcScope* scope_top; // the innermost running scope call: where `defer` puts its block



static void stack_check(VM* vm, int need, int line) {
    if (vm->sp + need >= PP_STACK_MAX) {
        rt_error(vm, line, "stack overflow (the value stack is full)");
    }
}

// a call of a generator: the arguments become the slots of a frame that next() starts; the generator
// replaces the arguments (and the callee) on the stack
static void make_generator(VM* vm, BcFunc* f, ObjClosure* closure, int argc, bool pop_callee) {
    ObjGenerator* g = gen_new(vm, GEN_BYTECODE, f->name ? f->name : str_cintern(vm, "generator"));
    g->bcfn = f;
    g->closure = (Obj*)closure;
    g->nslots = f->nlocals > argc ? f->nlocals : argc;
    g->slots = pp_calloc((size_t)(g->nslots + 1), sizeof(Value));
    for (int i = 0; i < g->nslots; ++i) {
        g->slots[i] = i < argc ? vm->stack[vm->sp - argc + i] : void_val();
    }

    vm->sp -= argc + (pop_callee ? 1 : 0);
    vm->stack[vm->sp++] = obj_val(g);
}

// starts a call of f with its arguments on the stack (argc of them, below vm->sp)
static void enter(VM* vm, BcFunc* f, ObjClosure* closure, int argc, int line, bool pop_callee) {
    if (f->generator) {
        make_generator(vm, f, closure, argc, pop_callee);

        return;
    }

    if (vm->fp >= PP_FRAMES_MAX) {
        rt_error(vm, line, "stack overflow (more than %d nested calls)", PP_FRAMES_MAX);
    }

    if (f->kind == BC_BLOCK && (!closure || !closure->scope)) {
        rt_error(vm, line, "the Block of '%s' can only run while its scope call runs (a Callback<T> can be kept for later)",
                 f->scope->chars);
    }

    if (!f->is_init) {
        Frame* tf = &vm->frames[vm->fp++];
        tf->name = f->name ? f->name->chars : "?";
        tf->file = vm->cur_file;
        tf->line = line;
    }

    stack_check(vm, f->nlocals + 64, line);
    if (nframes >= capframes) {
        capframes = capframes ? capframes * 2 : 64;
        frames = pp_realloc(frames, sizeof(BcFrame) * (size_t)capframes);
    }

    BcFrame* fr = &frames[nframes++];
    fr->fn = f;
    fr->closure = closure;
    fr->base = vm->sp - argc;
    fr->ip = 0;
    fr->pop_callee = pop_callee;
    fr->traced = !f->is_init;
    fr->saved_file = vm->cur_file;
    fr->gen = NULL;
    for (int i = argc; i < f->nlocals; ++i) {
        vm->stack[vm->sp++] = void_val();
    }

    vm->cur_file = f->file;
}

static bool str_eq(const ObjString* a, const ObjString* b) {
    return a == b || (a->len == b->len && memcmp(a->chars, b->chars, (size_t)a->len) == 0);
}

static int str_cmp(const ObjString* a, const ObjString* b) {
    const int n = a->len < b->len ? a->len : b->len;
    const int c = memcmp(a->chars, b->chars, (size_t)n);
    if (c != 0) {
        return c;
    }

    return a->len - b->len;
}

// does the value have static type t (for `as`)
static bool fits(SType* t, Value v) {
    switch (t->kind) {
        case ST_UNKNOWN: return v.kind != VAL_VOID;
        case ST_INT: return v.kind == VAL_INT;
        case ST_FLOAT: return v.kind == VAL_FLOAT || v.kind == VAL_INT;
        case ST_BOOL: return v.kind == VAL_BOOL;
        case ST_STRING: return IS_KIND(v, OBJ_STRING);
        case ST_RANGE: {
            if (!IS_KIND(v, OBJ_RANGE)) {
                return false;
            }

            const ObjRange* r = AS_RANGE(v);
            if (!t->elem) {
                return true;
            }

            if (t->elem->kind == ST_INT || t->elem->kind == ST_FLOAT) {
                return range_is_numeric(r) && range_is_float(r) == (t->elem->kind == ST_FLOAT);
            }

            return !range_is_numeric(r) && fits(t->elem, r->start);
        }
        case ST_OBJECT: return IS_KIND(v, OBJ_OBJECT);
        case ST_FUNC:
        case ST_FUNCTION: return is_callable(v);
        case ST_ARRAY: {
            if (!IS_KIND(v, OBJ_ARRAY)) {
                return false;
            }

            const ObjArray* a = AS_ARRAY(v);
            for (int i = 0; i < a->count; ++i) {
                if (!fits(t->elem, a->items[i])) {
                    return false;
                }
            }

            return true;
        }
        case ST_TUPLE: {
            if (!IS_KIND(v, OBJ_TUPLE) || AS_TUPLE(v)->count != t->nargs) {
                return false;
            }

            for (int i = 0; i < t->nargs; ++i) {
                if (!fits(t->args[i], AS_TUPLE(v)->items[i])) {
                    return false;
                }
            }

            return true;
        }
        case ST_UNION:
            for (int i = 0; i < t->nargs; ++i) {
                if (fits(t->args[i], v)) {
                    return true;
                }
            }

            return false;
        case ST_RECORD: {
            if (!IS_KIND(v, OBJ_OBJECT)) {
                return false;
            }

            ObjObject* o = AS_OBJECT(v);
            if (t->decl) {
                if (o->tag && str_eq(o->tag->name, t->decl->name)) {
                    return true;
                }

                st_record_fill(prog->checker, t);
            }

            for (int i = 0; i < t->nfields; ++i) {
                const Entry* e = table_find(&o->fields, t->fnames[i]);
                if (!e || !fits(t->ftypes[i], e->value)) {
                    return false;
                }
            }

            return true;
        }
        case ST_ENUM:
            if (!IS_KIND(v, OBJ_ENUM_VALUE)) {
                return false;
            }

            for (int i = 0; i < prog->nenums; ++i) {
                if (prog->enum_decls[i] == t->decl) {
                    return AS_ENUMV(v)->en == prog->enums[i];
                }
            }

            return str_eq(AS_ENUMV(v)->en->name, t->decl->name);
        case ST_NATIVE:
            if (t->decl->which == NT_MAP) {
                return IS_KIND(v, OBJ_MAP);
            }

            if (t->decl->which == NT_SET) {
                return IS_KIND(v, OBJ_SET);
            }

            if (t->decl->which == NT_GENERATOR) {
                return IS_KIND(v, OBJ_GENERATOR);
            }

            return IS_KIND(v, OBJ_INSTANCE) && str_eq(AS_INSTANCE(v)->type->name, t->decl->name);
        default: return false;
    }
}

static int utf8_len(unsigned char c) {
    if (c < 0x80) {
        return 1;
    }

    if ((c >> 5) == 6) {
        return 2;
    }

    if ((c >> 4) == 14) {
        return 3;
    }

    if ((c >> 3) == 30) {
        return 4;
    }

    return 1;
}

// record: the declaration's record type, for the T? fields a literal left out (they become none())
static void tag_value(VM* vm, Value v, ObjTypeDef* td, const Type* record) {
    if (IS_KIND(v, OBJ_OBJECT)) {
        if (!AS_OBJECT(v)->tag) {
            AS_OBJECT(v)->tag = td;
        }

        record_convert(vm, AS_OBJECT(v), record);

        return;
    }

    if (IS_KIND(v, OBJ_ARRAY)) {
        const ObjArray* a = AS_ARRAY(v);
        for (int i = 0; i < a->count; ++i) {
            tag_value(vm, a->items[i], td, record);
        }
    }
}

// the next item of a for loop: false when the iterable is done
static bool iter_next(VM* vm, Value* slots, int line, Value* key, Value* item) {
    const Value it = slots[0];
    const int64_t pos = slots[1].as.i;
    const int64_t idx = slots[2].as.i;
    *key = int_val(idx);
    if (IS_KIND(it, OBJ_RANGE)) {
        ObjRange* r = AS_RANGE(it);
        if (!range_is_numeric(r)) {
            rt_error(vm, line, "a range of %s is iterated through Iterable<T>: give the value its type, Range<T>",
                     value_type_name(r->start));
        }

        if (range_is_float(r)) {
            if (pos >= range_length(r)) {
                return false;
            }

            *item = range_at(r, pos);
        } else {
            const int64_t v = r->start.as.i + pos * r->step.as.i;
            if (r->step.as.i > 0 ? v >= r->end.as.i : v <= r->end.as.i) {
                return false;
            }

            *item = int_val(v);
        }

        *key = int_val(pos);
        slots[1] = int_val(pos + 1);

        return true;
    }

    if (IS_KIND(it, OBJ_ARRAY)) {
        const ObjArray* a = AS_ARRAY(it);
        if (pos >= a->count) {
            return false;
        }

        *item = a->items[pos];
        *key = int_val(pos);
        slots[1] = int_val(pos + 1);

        return true;
    }

    if (IS_KIND(it, OBJ_STRING)) {
        ObjString* s = AS_STR(it);
        if (pos >= s->len) {
            return false;
        }

        int len = utf8_len((unsigned char)s->chars[pos]);
        if (pos + len > s->len) {
            len = s->len - (int)pos;
        }

        *item = obj_val(str_new(vm, s->chars + pos, len));
        slots[1] = int_val(pos + len);
        slots[2] = int_val(idx + 1);

        return true;
    }

    if (IS_KIND(it, OBJ_TUPLE)) {
        const ObjTuple* t = AS_TUPLE(it);
        if (pos >= t->count) {
            return false;
        }

        *item = t->items[pos];
        *key = int_val(pos);
        slots[1] = int_val(pos + 1);

        return true;
    }

    if (IS_KIND(it, OBJ_MAP) || IS_KIND(it, OBJ_SET)) {
        const ObjMap* m = AS_MAP(it);
        int64_t i = pos;
        while (i < m->count && !m->entries[i].live) {
            i++;
        }

        if (i >= m->count) {
            return false;
        }

        if (IS_KIND(it, OBJ_MAP)) {
            *key = m->entries[i].key;
            *item = m->entries[i].value;
        } else {
            *item = m->entries[i].key;
        }

        slots[1] = int_val(i + 1);
        slots[2] = int_val(idx + 1);

        return true;
    }

    if (IS_KIND(it, OBJ_OBJECT)) {
        const ObjObject* o = AS_OBJECT(it);
        if (pos >= o->fields.count) {
            return false;
        }

        *key = obj_val(o->fields.entries[pos].key);
        *item = o->fields.entries[pos].value;
        slots[1] = int_val(pos + 1);

        return true;
    }

    if (IS_KIND(it, OBJ_GENERATOR)) {
        if (!gen_next(vm, (ObjGenerator*)it.as.o, item, line)) {
            return false;
        }

        slots[2] = int_val(idx + 1);

        return true;
    }

    if (IS_KIND(it, OBJ_INSTANCE)) {
        ObjInstance* inst = AS_INSTANCE(it);
        const NativeClass* cls = inst->type->cls;
        if (!cls->length || !cls->get || pos >= cls->length(inst)) {
            return false;
        }

        *item = cls->get(vm, inst, int_val(pos), line);
        *key = int_val(pos);
        slots[1] = int_val(pos + 1);

        return true;
    }

    rt_error(vm, line, "cannot iterate over a value of type %s", value_type_name(it));
}

static Value run(VM* vm, int stop);

// calls a compiled function from C: a module's top level, or a closure a native calls
static Value invoke(VM* vm, BcFunc* f, ObjClosure* closure, int argc, Value* args, int line) {
    const int depth = nframes;
    stack_check(vm, argc + 2, line);
    vm->stack[vm->sp++] = closure ? obj_val(closure) : void_val();
    for (int i = 0; i < argc; ++i) {
        vm->stack[vm->sp++] = args[i];
    }

    enter(vm, f, closure, argc, line, true);
    if (f->generator) {
        return vm->stack[--vm->sp]; // a generator: built, not run
    }

    return run(vm, depth);
}

// next() of a bytecode generator: its frame goes back on the stack and runs to OP_SUSPEND / OP_GEN_END
static Value gen_resume(VM* vm, ObjGenerator* g, bool* done) {
    BcFunc* f = (BcFunc*)g->bcfn;
    const int depth = nframes;
    g->state = GEN_RUNNING;
    if (vm->fp >= PP_FRAMES_MAX) {
        rt_error(vm, 0, "stack overflow (more than %d nested calls)", PP_FRAMES_MAX);
    }

    Frame* tf = &vm->frames[vm->fp++];
    tf->name = f->name ? f->name->chars : "?";
    tf->file = vm->cur_file;
    tf->line = vm->cur_line;
    stack_check(vm, g->nslots + 64, vm->cur_line);
    if (nframes >= capframes) {
        capframes = capframes ? capframes * 2 : 64;
        frames = pp_realloc(frames, sizeof(BcFrame) * (size_t)capframes);
    }

    BcFrame* fr = &frames[nframes++];
    fr->fn = f;
    fr->closure = (ObjClosure*)g->closure;
    fr->base = vm->sp;
    fr->ip = g->ip;
    fr->pop_callee = false;
    fr->traced = true;
    fr->saved_file = vm->cur_file;
    fr->gen = g;
    for (int i = 0; i < g->nslots; ++i) {
        vm->stack[vm->sp++] = g->slots[i];
    }

    vm->cur_file = f->file;
    const int saved_line = vm->cur_line;
    const Value r = run(vm, depth);
    vm->cur_line = saved_line;
    *done = g->state == GEN_DONE && !g->finish;

    return r;
}

Value bc_call_closure(VM* vm, Obj* closure, int argc, Value* args, int line) {
    ObjClosure* cl = (ObjClosure*)closure;

    return invoke(vm, (BcFunc*)cl->fn, cl, argc, args, line);
}

// Runs the scope function of a scope call; its arguments (the block first) are on the stack. A
// block that leaves early longjmps back here with the frames of the scope function still on top,
// and they are cut back to the call. Kept out of run() so the setjmp does not clobber its locals.
static void run_scope(VM* vm, BcFunc* f, ObjClosure* callee, int argc, int line, BcScope* sc) {
    if (setjmp(sc->jb)) {
        nframes = sc->nframes;
        vm->fp = sc->fp;
        vm->err_jmp = sc->err_jmp;
        vm->gc_disabled = sc->gc_disabled;
        vm->cur_file = sc->file;

        return;
    }

    const int depth = nframes;
    enter(vm, f, callee, argc, line, false);
    vm->stack[sc->slot] = run(vm, depth);
}

#define PUSH(V)                                                                                                        \
    do {                                                                                                               \
        const Value pushed_ = (V);                                                                                     \
        vm->stack[vm->sp++] = pushed_;                                                                                 \
    } while (0)
#define POP() (vm->stack[--vm->sp])
#define PEEK(N) (vm->stack[vm->sp - 1 - (N)])
#define READ() (code[fr->ip++])
#define LOCAL(S) (vm->stack[fr->base + (S)])
#define BIN_I(EXPR)                                                                                                    \
    do {                                                                                                               \
        const int64_t b = POP().as.i;                                                                                  \
        const int64_t a = POP().as.i;                                                                                  \
        PUSH(EXPR);                                                                                                    \
    } while (0)
#define BIN_F(EXPR)                                                                                                    \
    do {                                                                                                               \
        const double b = POP().as.f;                                                                                   \
        const double a = POP().as.f;                                                                                   \
        PUSH(EXPR);                                                                                                    \
    } while (0)
#define BIN_S(EXPR)                                                                                                    \
    do {                                                                                                               \
        const ObjString* b = AS_STR(POP());                                                                            \
        const ObjString* a = AS_STR(POP());                                                                            \
        PUSH(EXPR);                                                                                                    \
    } while (0)

// runs frames until the frame count drops back to `stop`; returns the value the last one returned
static Value run(VM* vm, int stop) {
    BcFrame* fr = &frames[nframes - 1];
    const int32_t* code = fr->fn->code;
    for (;;) {
        const int32_t op = READ();
        switch (op) {
            case OP_INT: PUSH(int_val(READ())); break;
            case OP_CONST: PUSH(prog->consts[READ()]); break;
            case OP_TRUE: PUSH(bool_val(true)); break;
            case OP_FALSE: PUSH(bool_val(false)); break;
            case OP_VOID: PUSH(void_val()); break;
            case OP_POP: vm->sp--; break;
            case OP_DUP: PUSH(PEEK(0)); break;
            case OP_LOAD: PUSH(LOCAL(READ())); break;
            case OP_STORE: LOCAL(READ()) = POP(); break;
            case OP_GLOAD: PUSH(vm->stack[READ()]); break;
            case OP_GSTORE: vm->stack[READ()] = POP(); break;
            case OP_CAP: PUSH(fr->closure->caps[READ()]); break;
            case OP_BOX: {
                const int s = READ();
                ObjArray* cell = array_new(vm, 1);
                array_push(cell, LOCAL(s));
                LOCAL(s) = obj_val(cell);
                break;
            }
            case OP_CELL_GET: {
                const Value cell = POP();
                PUSH(AS_ARRAY(cell)->items[0]);
                break;
            }
            case OP_CELL_SET: {
                const Value cell = POP();
                AS_ARRAY(cell)->items[0] = POP();
                break;
            }

            case OP_ADD_I: BIN_I(int_val((int64_t)((uint64_t)a + (uint64_t)b))); break;
            case OP_SUB_I: BIN_I(int_val((int64_t)((uint64_t)a - (uint64_t)b))); break;
            case OP_MUL_I: BIN_I(int_val((int64_t)((uint64_t)a * (uint64_t)b))); break;
            case OP_DIV_I: {
                const int64_t b = POP().as.i;
                const int64_t a = POP().as.i;
                if (b == 0) {
                    rt_error(vm, 0, "integer division by zero");
                }

                PUSH(int_val(a == INT64_MIN && b == -1 ? INT64_MIN : a / b));
                break;
            }
            case OP_MOD_I: {
                const int64_t b = POP().as.i;
                const int64_t a = POP().as.i;
                if (b == 0) {
                    rt_error(vm, 0, "integer modulo by zero");
                }

                int64_t r = b == -1 ? 0 : a % b;
                if (r != 0 && ((r < 0) != (b < 0))) {
                    r += b;
                }

                PUSH(int_val(r));
                break;
            }
            case OP_NEG_I: PUSH(int_val((int64_t)(0 - (uint64_t)POP().as.i))); break;
            case OP_POW_I: {
                int64_t e = POP().as.i;
                const int64_t base0 = POP().as.i;
                if (e < 0) {
                    rt_error(vm, 0, "int ** int needs an exponent >= 0, got %" PRId64 "; use a float base: %" PRId64 ".0 ** %" PRId64,
                             e, base0, e);
                }

                uint64_t base = (uint64_t)base0, result = 1;
                while (e > 0) {
                    if (e & 1) {
                        result *= base;
                    }

                    base *= base;
                    e >>= 1;
                }

                PUSH(int_val((int64_t)result));
                break;
            }
            case OP_ADD_F: BIN_F(float_val(a + b)); break;
            case OP_SUB_F: BIN_F(float_val(a - b)); break;
            case OP_MUL_F: BIN_F(float_val(a * b)); break;
            case OP_DIV_F: BIN_F(float_val(a / b)); break;
            case OP_MOD_F: {
                const double b = POP().as.f;
                const double a = POP().as.f;
                double r = fmod(a, b);
                if (r != 0 && ((r < 0) != (b < 0))) {
                    r += b;
                }

                PUSH(float_val(r));
                break;
            }
            case OP_NEG_F: PUSH(float_val(-POP().as.f)); break;
            case OP_POW_F: BIN_F(float_val(pow(a, b))); break;
            case OP_I2F: {
                const Value v = POP();
                PUSH(v.kind == VAL_INT ? float_val((double)v.as.i) : v);
                break;
            }
            case OP_I2F_UNDER: {
                const Value v = PEEK(1);
                if (v.kind == VAL_INT) {
                    PEEK(1) = float_val((double)v.as.i);
                }

                break;
            }
            case OP_EQ_I: BIN_I(bool_val(a == b)); break;
            case OP_NE_I: BIN_I(bool_val(a != b)); break;
            case OP_LT_I: BIN_I(bool_val(a < b)); break;
            case OP_GT_I: BIN_I(bool_val(a > b)); break;
            case OP_LE_I: BIN_I(bool_val(a <= b)); break;
            case OP_GE_I: BIN_I(bool_val(a >= b)); break;
            case OP_EQ_F: BIN_F(bool_val(a == b)); break;
            case OP_NE_F: BIN_F(bool_val(a != b)); break;
            case OP_LT_F: BIN_F(bool_val(a < b)); break;
            case OP_GT_F: BIN_F(bool_val(a > b)); break;
            case OP_LE_F: BIN_F(bool_val(a <= b)); break;
            case OP_GE_F: BIN_F(bool_val(a >= b)); break;
            case OP_EQ_S: BIN_S(bool_val(str_eq(a, b))); break;
            case OP_NE_S: BIN_S(bool_val(!str_eq(a, b))); break;
            case OP_LT_S: BIN_S(bool_val(str_cmp(a, b) < 0)); break;
            case OP_GT_S: BIN_S(bool_val(str_cmp(a, b) > 0)); break;
            case OP_LE_S: BIN_S(bool_val(str_cmp(a, b) <= 0)); break;
            case OP_GE_S: BIN_S(bool_val(str_cmp(a, b) >= 0)); break;
            case OP_EQ_B: {
                const bool b = POP().as.b;
                const bool a = POP().as.b;
                PUSH(bool_val(a == b));
                break;
            }
            case OP_NE_B: {
                const bool b = POP().as.b;
                const bool a = POP().as.b;
                PUSH(bool_val(a != b));
                break;
            }
            case OP_NOT: PUSH(bool_val(!POP().as.b)); break;
            case OP_CONCAT: {
                const ObjString* b = AS_STR(PEEK(0));
                const ObjString* a = AS_STR(PEEK(1));
                char* buf = pp_malloc((size_t)(a->len + b->len + 1));
                memcpy(buf, a->chars, (size_t)a->len);
                memcpy(buf + a->len, b->chars, (size_t)b->len);
                const Value r = obj_val(str_new(vm, buf, a->len + b->len));
                free(buf);
                vm->sp -= 2;
                PUSH(r);
                break;
            }
            case OP_LEN_S: PUSH(int_val(AS_STR(POP())->len)); break;
            case OP_LEN_A: PUSH(int_val(AS_ARRAY(POP())->count)); break;
            case OP_VALUES_EQ: {
                const Value b = POP();
                const Value a = POP();
                PUSH(bool_val(values_equal(a, b)));
                break;
            }

            case OP_JUMP: fr->ip = code[fr->ip]; break;
            case OP_JUMP_FALSE: {
                const int t = READ();
                if (!POP().as.b) {
                    fr->ip = t;
                }

                break;
            }
            case OP_JUMP_TRUE: {
                const int t = READ();
                if (POP().as.b) {
                    fr->ip = t;
                }

                break;
            }
            case OP_AND: {
                const int t = READ();
                if (!PEEK(0).as.b) {
                    fr->ip = t;
                } else {
                    vm->sp--;
                }

                break;
            }
            case OP_OR: {
                const int t = READ();
                if (PEEK(0).as.b) {
                    fr->ip = t;
                } else {
                    vm->sp--;
                }

                break;
            }

            case OP_CALL: {
                BcFunc* f = prog->funcs[READ()];
                const int argc = READ();
                const int line = READ();
                enter(vm, f, NULL, argc, line, false);
                fr = &frames[nframes - 1];
                code = fr->fn->code; // the caller's own when f is a generator (built, not entered)
                break;
            }
            case OP_CALL_NATIVE: {
                const BcNative* nf = &prog->natives[READ()];
                const int argc = READ();
                const int line = READ();
                if (vm->fp >= PP_FRAMES_MAX) {
                    rt_error(vm, line, "stack overflow (more than %d nested calls)", PP_FRAMES_MAX);
                }

                Frame* tf = &vm->frames[vm->fp++];
                tf->name = nf->name;
                tf->file = vm->cur_file;
                tf->line = line;
                const int saved_line = vm->cur_line;
                vm->cur_line = line;
                const Value r = nf->ext ? extern_call(vm, nf->ext, argc, &vm->stack[vm->sp - argc])
                                        : nf->fn(vm, argc, &vm->stack[vm->sp - argc]);
                vm->cur_line = saved_line;
                vm->fp--;
                vm->sp -= argc;
                PUSH(r);
                fr = &frames[nframes - 1]; // a callback may have grown the frame array
                code = fr->fn->code;
                break;
            }
            case OP_CALL_VALUE: {
                const int argc = READ();
                const int line = READ();
                const Value callee = PEEK(argc);
                if (IS_KIND(callee, OBJ_CLOSURE)) {
                    ObjClosure* cl = (ObjClosure*)callee.as.o;
                    enter(vm, (BcFunc*)cl->fn, cl, argc, line, true);
                    fr = &frames[nframes - 1];
                    code = fr->fn->code;
                    break;
                }

                vm->cur_line = line;
                const Value r = vm_call(vm, callee, argc, &vm->stack[vm->sp - argc]);
                vm->sp -= argc + 1;
                PUSH(r);
                fr = &frames[nframes - 1];
                code = fr->fn->code;
                break;
            }
            case OP_RETURN: {
                const Value r = POP();
                vm->sp = fr->base - (fr->pop_callee ? 1 : 0);
                vm->cur_file = fr->saved_file;
                if (fr->traced) {
                    vm->fp--;
                }

                nframes--;
                if (nframes == stop) {
                    return r;
                }

                PUSH(r);
                fr = &frames[nframes - 1];
                code = fr->fn->code;
                break;
            }

            case OP_ARRAY: {
                const int n = READ();
                ObjArray* a = array_new(vm, n);
                for (int i = 0; i < n; ++i) {
                    array_push(a, vm->stack[vm->sp - n + i]);
                }

                vm->sp -= n;
                PUSH(obj_val(a));
                break;
            }
            case OP_TUPLE: {
                const int n = READ();
                ObjTuple* t = tuple_new(vm, n);
                for (int i = 0; i < n; ++i) {
                    t->items[i] = vm->stack[vm->sp - n + i];
                }

                vm->sp -= n;
                PUSH(obj_val(t));
                break;
            }
            case OP_OBJECT: PUSH(obj_val(object_new(vm))); break;
            case OP_FIELD_INIT: {
                ObjString* key = AS_STR(prog->consts[READ()]);
                const Value v = POP();
                table_set(&AS_OBJECT(PEEK(0))->fields, key, v);
                break;
            }
            case OP_GET_FIELD: {
                ObjString* key = AS_STR(prog->consts[READ()]);
                const int line = READ();
                const Value o = POP();
                PUSH(vm_get_field(vm, o, key, line));
                break;
            }
            case OP_SET_FIELD: {
                ObjString* key = AS_STR(prog->consts[READ()]);
                const int line = READ();
                const Value v = POP();
                const Value o = POP();
                vm_set_field(vm, o, key, v, line);
                break;
            }
            case OP_INDEX: {
                const int line = READ();
                const Value k = POP();
                const Value o = POP();
                PUSH(vm_index_get(vm, o, k, line));
                break;
            }
            case OP_INDEX_SET: {
                const int line = READ();
                const Value v = POP();
                const Value k = POP();
                const Value o = POP();
                vm_index_set(vm, o, k, v, line);
                break;
            }
            case OP_ENUM: {
                ObjEnum* en = prog->enums[READ()];
                const int variant = READ();
                const int n = READ();
                if (n == 0 && en->variants[variant].singleton) {
                    PUSH(obj_val(en->variants[variant].singleton));
                    break;
                }

                ObjEnumValue* ev = enumv_new(vm, en, variant, n);
                for (int i = 0; i < n; ++i) {
                    ev->payload[i] = vm->stack[vm->sp - n + i];
                }

                vm->sp -= n;
                PUSH(obj_val(ev));
                break;
            }
            case OP_IS_VARIANT: {
                const ObjEnum* en = prog->enums[READ()];
                const int variant = READ();
                const Value v = POP();
                PUSH(bool_val(IS_KIND(v, OBJ_ENUM_VALUE) && AS_ENUMV(v)->en == en && AS_ENUMV(v)->variant == variant));
                break;
            }
            case OP_PAYLOAD: {
                const int k = READ();
                const Value v = POP();
                PUSH(AS_ENUMV(v)->payload[k]);
                break;
            }
            case OP_IS_TUPLE: {
                const int n = READ();
                const Value v = POP();
                PUSH(bool_val(IS_KIND(v, OBJ_TUPLE) && AS_TUPLE(v)->count == n));
                break;
            }
            case OP_TUPLE_GET: {
                const int k = READ();
                const Value v = POP();
                PUSH(AS_TUPLE(v)->items[k]);
                break;
            }
            case OP_AS: {
                SType* t = prog->types[READ()];
                const Value v = POP();
                if (!fits(t, v)) {
                    PUSH(make_none(vm));
                } else if (t->kind == ST_FLOAT && v.kind == VAL_INT) {
                    PUSH(make_some(vm, float_val((double)v.as.i)));
                } else {
                    PUSH(make_some(vm, v));
                }

                break;
            }
            case OP_TAG: {
                const int k = READ();
                tag_value(vm, PEEK(0), prog->typedefs[k], prog->typedef_decls[k]->def);
                break;
            }

            case OP_ITER_INIT: {
                const int s = READ();
                LOCAL(s) = POP();
                LOCAL(s + 1) = int_val(0);
                LOCAL(s + 2) = int_val(0);
                break;
            }
            case OP_ITER_NEXT: {
                const int s = READ();
                const int a = READ();
                const int b = READ();
                const int target = READ();
                Value key, item;
                const bool more = iter_next(vm, &LOCAL(s), vm->cur_line, &key, &item);
                fr = &frames[nframes - 1]; // a generator may have run (and grown the frame array)
                code = fr->fn->code;
                if (!more) {
                    fr->ip = target;
                    break;
                }

                const Value it = LOCAL(s);
                if (b >= 0 && IS_KIND(it, OBJ_GENERATOR)) {
                    // for k, v over pairs
                    if (!IS_KIND(item, OBJ_TUPLE) || AS_TUPLE(item)->count != 2) {
                        rt_error(vm, vm->cur_line, "for k, v needs pairs: got a value of type %s", value_type_name(item));
                    }

                    key = AS_TUPLE(item)->items[0];
                    item = AS_TUPLE(item)->items[1];
                }

                const bool keyed = IS_KIND(it, OBJ_MAP) || IS_KIND(it, OBJ_OBJECT);
                if (b >= 0) {
                    LOCAL(a) = key;
                    LOCAL(b) = item;
                } else {
                    LOCAL(a) = keyed ? key : item;
                }

                break;
            }
            case OP_CLOSURE: {
                BcFunc* f = prog->funcs[READ()];
                const int n = READ();
                ObjClosure* cl = (ObjClosure*)alloc_obj(vm, sizeof(ObjClosure) + sizeof(Value) * (size_t)n, OBJ_CLOSURE);
                cl->fn = f;
                cl->name = f->name ? f->name : str_cintern(vm, "lambda");
                cl->scope = NULL;
                cl->ncaps = n;
                for (int i = 0; i < n; ++i) {
                    cl->caps[i] = vm->stack[vm->sp - n + i];
                }

                vm->sp -= n;
                PUSH(obj_val(cl));
                break;
            }
            case OP_RUN_MODULE: {
                BcModule* m = &prog->modules[READ()];
                if (m->done || m->running || !m->init) {
                    break;
                }

                m->running = true;
                invoke(vm, m->init, NULL, 0, NULL, vm->cur_line);
                m->running = false;
                m->done = true;
                fr = &frames[nframes - 1];
                code = fr->fn->code;
                break;
            }
            case OP_LINE:
                vm->cur_line = READ();
                gc_maybe(vm);
                break;
            case OP_NO_MATCH: {
                const int line = READ();
                StrBuf sb;
                sb_init(&sb);
                value_format(&sb, POP(), false, 0);
                rt_error(vm, line, "no match arm matched the value %s", sb.data);
            }
            case OP_FAIL: rt_error(vm, 0, "%s", AS_STR(prog->consts[READ()])->chars);
            case OP_DISPATCH: {
                const BcDispatch* d = &prog->dispatches[READ()];
                const int argc = READ();
                const int line = READ();
                Value* args = &vm->stack[vm->sp - argc];
                const BcCand* chosen = NULL;
                for (int k = 0; k < d->ncands && !chosen; ++k) {
                    const BcCand* cand = &d->cands[k];
                    bool ok = cand->variadic ? argc >= cand->nparams : argc == cand->nparams;
                    for (int i = 0; ok && i < cand->nparams; ++i) {
                        ok = cand->params[i]->kind == ST_IFACE || fits(cand->params[i], args[i]);
                    }

                    if (ok) {
                        chosen = cand;
                    }
                }

                if (!chosen) {
                    StrBuf sb;
                    sb_init(&sb);
                    for (int i = 0; i < argc; ++i) {
                        sb_printf(&sb, i ? ", %s" : "%s", value_type_name(args[i]));
                    }

                    rt_error(vm, line, "no overload of '%s' accepts (%s)", d->name->chars, sb.data ? sb.data : "");
                }

                for (int i = 0; i < chosen->nparams && i < argc; ++i) {
                    if (chosen->params[i]->kind == ST_FLOAT && args[i].kind == VAL_INT) {
                        args[i] = float_val((double)args[i].as.i);
                    }
                }

                if (!chosen->native) {
                    enter(vm, prog->funcs[chosen->index], NULL, argc, line, false);
                    fr = &frames[nframes - 1];
                    code = fr->fn->code;
                    break;
                }

                const BcNative* nf = &prog->natives[chosen->index];
                Frame* tf = &vm->frames[vm->fp++];
                tf->name = nf->name;
                tf->file = vm->cur_file;
                tf->line = line;
                const Value r = nf->ext ? extern_call(vm, nf->ext, argc, args) : nf->fn(vm, argc, args);
                vm->fp--;
                vm->sp -= argc;
                PUSH(r);
                fr = &frames[nframes - 1];
                code = fr->fn->code;
                break;
            }
            case OP_SCOPE_CALL: {
                const int fi = READ();
                const int argc = READ();
                const int line = READ();
                const int args = vm->sp - argc;
                BcScope sc;
                sc.prev = scope_top;
                sc.slot = args - 2;
                sc.nframes = nframes;
                sc.fp = vm->fp;
                sc.err_jmp = vm->err_jmp;
                sc.gc_disabled = vm->gc_disabled;
                sc.file = vm->cur_file;
                sc.exit = SCOPE_NORMAL;
                ObjClosure* block = (ObjClosure*)vm->stack[args].as.o;
                ObjClosure* callee = fi < 0 ? (ObjClosure*)vm->stack[args - 3].as.o : NULL;
                BcFunc* f = callee ? (BcFunc*)callee->fn : prog->funcs[fi];
                block->scope = &sc;
                scope_top = &sc;
                run_scope(vm, f, callee, argc, line, &sc);
                scope_top = sc.prev;
                block->scope = NULL;
                vm->sp = args;
                vm->cur_line = line;
                const Value d = vm->stack[sc.slot + 1];
                if (IS_KIND(d, OBJ_CLOSURE)) {
                    bc_call_closure(vm, d.as.o, 0, NULL, line);
                }

                vm->cur_line = line;
                const Value r = vm->stack[sc.slot];
                vm->sp = sc.slot - 1;
                PUSH(r);
                PUSH(int_val(sc.exit));
                fr = &frames[nframes - 1];
                code = fr->fn->code;
                break;
            }
            case OP_SCOPE_EXIT: {
                const ScopeExit ex = (ScopeExit)READ();
                const char* what = ex == SCOPE_BREAK ? "break" : ex == SCOPE_CONTINUE ? "continue" : "return";
                BcScope* sc = fr->closure ? (BcScope*)fr->closure->scope : NULL;
                if (fr->fn->kind == BC_CALLBACK) {
                    rt_error(vm, 0, "'%s' cannot leave the Callback of '%s': it may run after the scope call ended", what,
                             fr->fn->scope->chars);
                }

                if (!sc) {
                    rt_error(vm, 0, "the Block of '%s' can only run while its scope call runs", fr->fn->scope->chars);
                }

                sc->exit = ex;
                vm->stack[sc->slot] = ex == SCOPE_RETURN ? POP() : void_val();
                longjmp(sc->jb, 1);
            }
            case OP_DEFER: {
                const Value d = POP();
                if (!scope_top) {
                    rt_error(vm, 0, "'defer' can only run in the body of a scope");
                }

                vm->stack[scope_top->slot + 1] = d;
                break;
            }
            case OP_SUSPEND:
            case OP_GEN_END: {
                // the generator's frame leaves the stack: kept in the generator (paused), or dropped (done)
                ObjGenerator* g = fr->gen;
                const bool end = op == OP_GEN_END;
                const bool has_value = !end || READ() == 1;
                const Value r = has_value ? POP() : void_val();
                if (end) {
                    g->state = GEN_DONE;
                    g->finish = has_value;
                    free(g->slots);
                    g->slots = NULL;
                    g->nslots = 0;
                    g->closure = NULL;
                } else {
                    const int n = vm->sp - fr->base;
                    g->slots = pp_realloc(g->slots, sizeof(Value) * (size_t)(n + 1));
                    memcpy(g->slots, &vm->stack[fr->base], sizeof(Value) * (size_t)n);
                    g->nslots = n;
                    g->ip = fr->ip;
                    g->state = GEN_PAUSED;
                }

                vm->sp = fr->base;
                vm->cur_file = fr->saved_file;
                vm->fp--;
                nframes--;
                if (nframes != stop) {
                    rt_error(vm, 0, "internal error: a generator paused below another frame");
                }

                return r;
            }
            case OP_JUMP_ITERABLE: {
                const int target = READ();
                const Value v = PEEK(0);
                if ((IS_KIND(v, OBJ_RANGE) && range_is_numeric(AS_RANGE(v))) || IS_KIND(v, OBJ_ARRAY) || IS_KIND(v, OBJ_STRING) || IS_KIND(v, OBJ_TUPLE) ||
                    IS_KIND(v, OBJ_MAP) || IS_KIND(v, OBJ_SET) || IS_KIND(v, OBJ_INSTANCE) || IS_KIND(v, OBJ_GENERATOR)) {
                    fr->ip = target;
                }

                break;
            }
            default: rt_error(vm, 0, "internal error: bad opcode %d", op);
        }
    }
}

int bc_run(BcProgram* p) {
    prog = p;
    VM* vm = p->vm;
    vm->closure_call = bc_call_closure;
    vm->gen_resume = gen_resume;
    jmp_buf jb;
    jmp_buf* prev = vm->err_jmp;
    vm->err_jmp = &jb;
    if (setjmp(jb)) {
        vm->err_jmp = prev;
        fflush(stdout);

        return 1;
    }

    // the globals of every module sit at the bottom of the stack
    vm->sp = 0;
    vm->fp = 0;
    for (int i = 0; i < p->nglobals; ++i) {
        vm->stack[vm->sp++] = void_val();
    }

    BcModule* main = NULL;
    for (int i = 0; i < p->nmodules; ++i) {
        if (p->modules[i].module == p->checker->main) {
            main = &p->modules[i];
        }
    }

    vm->cur_file = main->init->file;
    main->running = true;
    invoke(vm, main->init, NULL, 0, NULL, 0);
    fflush(stdout);
    vm->err_jmp = prev;

    return 0;
}
