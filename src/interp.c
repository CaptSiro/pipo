/*
 * Tree-walking evaluator: scopes, calls & overload resolution, pattern
 * matching, modules and the top level entry points.
 */
#include <ctype.h>

#include "parser.h"
#include "vm.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <limits.h>
#include <unistd.h>
#endif

static Value eval(VM* vm, Node* n, ObjEnv* env);
static ExecStatus exec(VM* vm, Node* n, ObjEnv* env);
static ExecStatus exec_block(VM* vm, Node* b, ObjEnv* env, bool new_scope, Value* out);
static ExecStatus exec_branchy(VM* vm, Node* n, ObjEnv* env, Value* out);
static Value call_value(VM* vm, Value callee, int argc, Value* args, int line, InlineCache* ic);
static Value call_value_adl(VM* vm, Value callee, int argc, Value* args, int line, InlineCache* ic, ObjOverload* adl);

/* ------------------------------------------------------------------ errors */

void rt_error(VM* vm, int line, const char* fmt, ...) {
    fflush(stdout);
    if (line <= 0) line = vm->cur_line;
    fprintf(stderr, "%s:%d: error: ", vm->cur_file ? vm->cur_file : "<unknown>", line);
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
    if (vm->fp > 0) {
        fprintf(stderr, "stack traceback (most recent call last):\n");
        int start = 0;
        if (vm->fp > 24) {
            start = vm->fp - 24;
            fprintf(stderr, "  ... %d more calls\n", start);
        }
        for (int i = start; i < vm->fp; i++) {
            Frame* f = &vm->frames[i];
            fprintf(stderr, "  %s:%d: called '%s'\n", f->file ? f->file : "?", f->line, f->name);
        }
    }
    fflush(stderr);
    if (vm->err_jmp) longjmp(*vm->err_jmp, 1);
    exit(1);
}

/* ------------------------------------------------------------- vm stacks */

void vm_push(VM* vm, Value v) {
    if (vm->sp >= PP_STACK_MAX) rt_error(vm, 0, "value stack overflow");
    vm->stack[vm->sp++] = v;
}

static void push_env(VM* vm, ObjEnv* e) {
    if (vm->esp >= PP_ENV_STACK_MAX) rt_error(vm, 0, "scope stack overflow");
    vm->envs[vm->esp++] = e;
}

static void pop_env(VM* vm) {
    vm->esp--;
}

ObjEnv* vm_current_env(VM* vm) {
    return vm->esp > 0 ? vm->envs[vm->esp - 1] : vm->builtins;
}

void vm_register_ic(VM* vm, InlineCache* ic) {
    GROW(vm->caches, vm->ncaches, vm->capcaches);
    vm->caches[vm->ncaches++] = ic;
}

void vm_register_native_module(VM* vm, const char* name, NativeModuleInit init) {
    vm->native_modules = pp_realloc(vm->native_modules, sizeof(NativeModule) * (size_t)(vm->nnative_modules + 1));
    vm->native_modules[vm->nnative_modules].name = name;
    vm->native_modules[vm->nnative_modules].init = init;
    vm->nnative_modules++;
}

/* ------------------------------------------------------------------ scopes */

Entry* env_find(ObjEnv* env, ObjString* name, ObjEnv** where) {
    for (ObjEnv* e = env; e; e = e->parent) {
        Entry* en = table_find(&e->vars, name);
        if (en) {
            if (where) *where = e;
            return en;
        }
    }
    return NULL;
}

void env_define(VM* vm, ObjEnv* env, ObjString* name, Value v, uint8_t flags, Type* type) {
    (void)vm;
    Entry* e = table_set(&env->vars, name, v);
    e->flags = flags;
    e->type = type;
}

/*
 * Declares a function. Functions with the same name form an overload set; a
 * declaration in an inner scope extends (a copy of) the set visible from the
 * outer scope, so `function +(Vec a, Vec b)` keeps the built-in `+` working.
 */
void env_add_function(VM* vm, ObjEnv* env, ObjString* name, Obj* fn) {
    Entry* e = table_find(&env->vars, name);
    if (e) {
        if (IS_KIND(e->value, OBJ_OVERLOAD)) {
            overload_add((ObjOverload*)e->value.as.o, fn);
            return;
        }
        rt_error(vm, 0, "cannot declare function '%s': the name is already used by a %s value", name->chars,
                 value_type_name(e->value));
    }
    ObjOverload* set = overload_new(vm, name);
    env_define(vm, env, name, obj_val(set), VAR_CONST, NULL);
    Entry* outer = env->parent ? env_find(env->parent, name, NULL) : NULL;
    if (outer) {
        if (IS_KIND(outer->value, OBJ_OVERLOAD)) {
            ObjOverload* os = (ObjOverload*)outer->value.as.o;
            for (int i = 0; i < os->count; i++) overload_add(set, os->items[i]);
        } else if (IS_KIND(outer->value, OBJ_FUNCTION) || IS_KIND(outer->value, OBJ_NATIVE) ||
                   IS_KIND(outer->value, OBJ_VARIANT_CTOR)) {
            overload_add(set, outer->value.as.o);
        }
    }
    overload_add(set, fn);
}

/*
 * `global function f(...)` adds f to a VM-wide overload set as well as to its
 * module. Every lookup of the name merges that set in, so the function is
 * usable everywhere once the declaring file is loaded.
 */
static ObjOverload* global_set(VM* vm, ObjString* name) {
    if (vm->globals.count == 0) return NULL;
    Entry* e = table_find(&vm->globals, name);
    return e ? (ObjOverload*)e->value.as.o : NULL;
}

static void global_add(VM* vm, ObjString* name, Obj* fn) {
    ObjOverload* set = global_set(vm, name);
    if (!set) {
        set = overload_new(vm, name);
        table_set(&vm->globals, name, obj_val(set));
    }
    overload_add(set, fn);
}

/* the module whose scope (or a scope nested in it) is env */
static ObjModule* module_of_env(VM* vm, ObjEnv* env) {
    for (; env; env = env->parent) {
        for (int i = 0; i < vm->modules.count; i++) {
            ObjModule* m = (ObjModule*)vm->modules.entries[i].value.as.o;
            if (m->env == env) return m;
        }
    }
    return NULL;
}

/*
 * For recv.name(args): the overload set `name` exported by the module that
 * declared the receiver's type, so `r.area()` works without `r.shapes:area()`.
 */
static ObjOverload* receiver_module_set(VM* vm, Value recv, ObjString* name) {
    ObjModule* m = NULL;
    if (IS_KIND(recv, OBJ_OBJECT)) {
        ObjTypeDef* td = AS_OBJECT(recv)->tag;
        if (td) m = td->module;
    } else if (IS_KIND(recv, OBJ_ENUM_VALUE)) {
        m = AS_ENUMV(recv)->en->module;
    }
    Value v;
    if (!m || !module_member(m, name, &v) || !IS_KIND(v, OBJ_OVERLOAD)) return NULL;
    return (ObjOverload*)v.as.o;
}

/* ------------------------------------------------------- option / error */

static int variant_index(ObjEnum* en, const char* name) {
    for (int i = 0; i < en->nvariants; i++)
        if (str_equals(en->variants[i].name, name)) return i;
    return -1;
}

static Value make_variant1(VM* vm, ObjEnum* en, const char* name, Value v) {
    ObjEnumValue* ev = enumv_new(vm, en, variant_index(en, name), 1);
    ev->payload[0] = v;
    return obj_val(ev);
}

Value make_some(VM* vm, Value v) {
    return make_variant1(vm, vm->option_enum, "some", v);
}

Value make_none(VM* vm) {
    return obj_val(vm->option_enum->variants[variant_index(vm->option_enum, "none")].singleton);
}

Value make_success(VM* vm, Value v) {
    return make_variant1(vm, vm->error_enum, "success", v);
}

Value make_fail(VM* vm, Value v) {
    return make_variant1(vm, vm->error_enum, "fail", v);
}

/* ------------------------------------------------------------------ calls */

static Signature* callable_sig(Obj* o) {
    switch (o->kind) {
        case OBJ_FUNCTION: return ((ObjFunction*)o)->sig;
        case OBJ_NATIVE: return ((ObjNative*)o)->sig;
        case OBJ_VARIANT_CTOR: return ((ObjVariantCtor*)o)->sig;
        default: return NULL;
    }
}

static ObjEnv* callable_env(VM* vm, Obj* o) {
    switch (o->kind) {
        case OBJ_FUNCTION: return ((ObjFunction*)o)->closure;
        case OBJ_VARIANT_CTOR: return ((ObjVariantCtor*)o)->en->env;
        default: return vm->builtins;
    }
}

static void init_bindings(Bindings* b, Signature* sig) {
    b->count = sig->ngenerics;
    for (int i = 0; i < sig->ngenerics; i++) {
        memset(&b->items[i], 0, sizeof(Binding));
        b->items[i].name = sig->generics[i];
    }
}

/* how well the arguments fit the candidate, 0 = not applicable */
static int score_candidate(VM* vm, Obj* c, int argc, Value* args, Bindings* b) {
    Signature* sig = callable_sig(c);
    if (!sig) return 0;
    if (sig->ngenerics > PP_MAX_BINDINGS) rt_error(vm, sig->line, "too many generic parameters (max %d)", PP_MAX_BINDINGS);
    if (sig->variadic ? argc < sig->nparams : argc != sig->nparams) return 0;
    init_bindings(b, sig);
    ObjEnv* env = callable_env(vm, c);
    int total = 1;
    for (int i = 0; i < sig->nparams; i++) {
        int s = sig->params[i].type ? type_match(vm, sig->params[i].type, args[i], env, b, false) : 1;
        if (!s) return 0;
        total += s;
    }
    return total;
}

static PP_NORETURN void no_overload_error_sets(VM* vm, ObjString* name, ObjOverload** sets, int nsets, Obj** cands, int ncands,
                                                int argc, Value* args, int line) {
    StrBuf sb;
    sb_init(&sb);
    sb_printf(&sb, "no overload of '%s' accepts (", name->chars);
    for (int i = 0; i < argc; i++) {
        if (i) sb_puts(&sb, ", ");
        sb_puts(&sb, value_type_name(args[i]));
    }
    sb_puts(&sb, ")\n  candidates:");
    for (int k = -1; k < nsets; k++) {
        Obj** items = k < 0 ? cands : sets[k]->items;
        int count = k < 0 ? ncands : sets[k]->count;
        for (int i = 0; i < count; i++) {
            Signature* sig = callable_sig(items[i]);
            if (!sig) continue;
            sb_puts(&sb, "\n    ");
            sig_to_string(sig, &sb);
        }
    }
    rt_error(vm, line, "%s", sb.data);
}

static PP_NORETURN void no_overload_error(VM* vm, ObjString* name, Obj** cands, int ncands, int argc, Value* args, int line) {
    no_overload_error_sets(vm, name, NULL, 0, cands, ncands, argc, args, line);
}

static bool ic_kinds(int argc, Value* args, uint8_t* kinds) {
    if (argc > 4) return false;
    for (int i = 0; i < argc; i++) {
        switch (args[i].kind) {
            case VAL_VOID: kinds[i] = 1; break;
            case VAL_BOOL: kinds[i] = 2; break;
            case VAL_INT: kinds[i] = 3; break;
            case VAL_FLOAT: kinds[i] = 4; break;
            case VAL_OBJ:
                if (args[i].as.o->kind != OBJ_STRING) return false;
                kinds[i] = 5;
                break;
        }
    }
    return true;
}

/*
 * Picks the best candidate from the set in scope, then the receiver's module
 * set (dot calls only), then the global set. Earlier sets win ties, so a
 * local declaration shadows an equally good global one.
 */
static Obj* resolve_overload(VM* vm, ObjOverload* set, ObjOverload* adl, int argc, Value* args, InlineCache* ic, Bindings* out,
                             int line) {
    ObjOverload* sets[IC_SETS] = {set, NULL, NULL};
    int nsets = 1;
    if (adl && adl != set) sets[nsets++] = adl;
    ObjOverload* g = global_set(vm, set->name);
    if (g && g != set && g != adl) sets[nsets++] = g;

    uint8_t kinds[4];
    bool cacheable = ic && ic_kinds(argc, args, kinds);
    bool hit = cacheable && ic->argc == argc && memcmp(ic->kinds, kinds, (size_t)argc) == 0;
    for (int k = 0; hit && k < IC_SETS; k++)
        hit = ic->sets[k] == (Obj*)sets[k] && ic->counts[k] == (sets[k] ? sets[k]->count : 0);
    if (hit) {
        Obj* t = ic->target;
        if (t->kind == OBJ_NATIVE) out->count = 0;
        else score_candidate(vm, t, argc, args, out);
        return t;
    }
    Obj* best = NULL;
    int best_score = 0;
    Bindings tmp;
    /* later declarations win ties, so a redefinition replaces an older one */
    for (int k = 0; k < nsets; k++) {
        ObjOverload* cs = sets[k];
        for (int i = cs->count - 1; i >= 0; i--) {
            int s = score_candidate(vm, cs->items[i], argc, args, &tmp);
            if (s > best_score) {
                best_score = s;
                best = cs->items[i];
                *out = tmp;
            }
        }
    }
    if (!best) no_overload_error_sets(vm, set->name, sets, nsets, NULL, 0, argc, args, line);
    if (cacheable) {
        for (int k = 0; k < IC_SETS; k++) {
            ic->sets[k] = (Obj*)sets[k];
            ic->counts[k] = sets[k] ? sets[k]->count : 0;
        }
        ic->argc = argc;
        memcpy(ic->kinds, kinds, (size_t)argc);
        ic->target = best;
    }
    return best;
}

static void push_frame(VM* vm, const char* name, int line) {
    if (vm->fp >= PP_FRAMES_MAX) rt_error(vm, line, "stack overflow (more than %d nested calls)", PP_FRAMES_MAX);
    Frame* f = &vm->frames[vm->fp++];
    f->name = name;
    f->file = vm->cur_file;
    f->line = line > 0 ? line : vm->cur_line;
}

static void maybe_tag(VM* vm, Type* t, Value v, ObjEnv* env) {
    (void)vm;
    if (!t || t->kind != TY_NAME || !IS_KIND(v, OBJ_OBJECT)) return;
    ObjObject* o = AS_OBJECT(v);
    if (o->tag) return;
    Value tv;
    if (resolve_type_value(vm, t, env, &tv) && IS_KIND(tv, OBJ_TYPEDEF)) {
        ObjTypeDef* td = (ObjTypeDef*)tv.as.o;
        if (td->def->kind == TY_RECORD) o->tag = td;
    }
}

/* lambdas stored in fields of a record type take the callback types of those fields */
static void adopt_field_callbacks(VM* vm, ObjObject* o) {
    ObjTypeDef* td = o->tag;
    if (!td || td->def->kind != TY_RECORD) return;
    for (int i = 0; i < td->def->nfields; i++) {
        Entry* e = table_find(&o->fields, td->def->fnames[i]);
        if (e && IS_KIND(e->value, OBJ_FUNCTION))
            e->value = adopt_callback_types(vm, td->def->ftypes[i], e->value, td->env, NULL, td->def->fnames[i]);
    }
}

/* name: the variable / field that receives the value, names an adopting lambda (may be NULL) */
static Value coerce_to(VM* vm, Type* t, Value v, ObjEnv* env, Bindings* b, bool deep, const char* what, int line,
                       ObjString* name) {
    if (t && t->kind == TY_PRIM && t->prim == PRIM_FLOAT && v.kind == VAL_INT) return float_val((double)v.as.i);
    if (!type_match(vm, t, v, env, b, deep)) {
        StrBuf sb;
        sb_init(&sb);
        type_to_string(t, &sb);
        rt_error(vm, line, "%s: expected %s but got %s", what, sb.data, value_type_name(v));
    }
    maybe_tag(vm, t, v, env);
    if (IS_KIND(v, OBJ_OBJECT)) adopt_field_callbacks(vm, AS_OBJECT(v));
    return adopt_callback_types(vm, t, v, env, b, name);
}

static Value call_function(VM* vm, ObjFunction* fn, int argc, Value* args, Bindings* b, int line) {
    Signature* sig = fn->sig;
    push_frame(vm, sig->name->chars, line);
    const char* saved_file = vm->cur_file;
    int saved_line = vm->cur_line;
    ObjEnv* env = env_new(vm, fn->closure);
    push_env(vm, env);
    for (int i = 0; i < sig->nparams && i < argc; i++) {
        Value a = args[i];
        Type* pt = sig->params[i].type;
        if (pt && pt->kind == TY_PRIM && pt->prim == PRIM_FLOAT && a.kind == VAL_INT) a = float_val((double)a.as.i);
        a = adopt_callback_types(vm, pt, a, fn->closure, b, sig->params[i].name);
        env_define(vm, env, sig->params[i].name, a, 0, sig->ngenerics ? NULL : pt);
    }
    vm->cur_file = sig->file;
    Value last = void_val();
    ExecStatus st = exec_block(vm, fn->body, env, false, sig->implicit_result ? &last : NULL);
    Value result = st == EX_RETURN ? vm->ret : last;
    vm->ret = void_val();
    if (st == EX_BREAK || st == EX_CONTINUE)
        rt_error(vm, 0, "'%s' used outside of a loop", st == EX_BREAK ? "break" : "continue");
    if (sig->ret) {
        if (sig->ret->kind == TY_PRIM && sig->ret->prim == PRIM_VOID) {
            if (result.kind != VAL_VOID)
                rt_error(vm, 0, "function '%s' is declared '-> void' but returned a %s", sig->name->chars, value_type_name(result));
        } else {
            if (result.kind == VAL_VOID) {
                StrBuf sb;
                sb_init(&sb);
                type_to_string(sig->ret, &sb);
                rt_error(vm, 0, "function '%s' must return a value of type %s", sig->name->chars, sb.data);
            }
            char what[160];
            snprintf(what, sizeof what, "return value of '%s'", sig->name->chars);
            result = coerce_to(vm, sig->ret, result, fn->closure, b, false, what, 0, NULL);
        }
    }
    pop_env(vm);
    vm->fp--;
    vm->cur_file = saved_file;
    vm->cur_line = saved_line;
    return result;
}

static Value invoke(VM* vm, Obj* target, int argc, Value* args, Bindings* b, int line) {
    switch (target->kind) {
        case OBJ_NATIVE: {
            ObjNative* nf = (ObjNative*)target;
            push_frame(vm, nf->sig->name->chars, line);
            int saved_line = vm->cur_line;
            if (line > 0) vm->cur_line = line;
            Value r = nf->fn(vm, argc, args);
            vm->cur_line = saved_line;
            vm->fp--;
            return r;
        }
        case OBJ_VARIANT_CTOR: {
            ObjVariantCtor* c = (ObjVariantCtor*)target;
            Variant* var = &c->en->variants[c->index];
            if (var->npayload == 0) return obj_val(var->singleton);
            ObjEnumValue* ev = enumv_new(vm, c->en, c->index, argc);
            for (int i = 0; i < argc; i++) ev->payload[i] = args[i];
            return obj_val(ev);
        }
        case OBJ_FUNCTION: return call_function(vm, (ObjFunction*)target, argc, args, b, line);
        default: rt_error(vm, line, "value is not callable");
    }
}

static Value call_value(VM* vm, Value callee, int argc, Value* args, int line, InlineCache* ic) {
    return call_value_adl(vm, callee, argc, args, line, ic, NULL);
}

/* adl: extra candidates from the receiver's module for a dot call, or NULL */
static Value call_value_adl(VM* vm, Value callee, int argc, Value* args, int line, InlineCache* ic, ObjOverload* adl) {
    if (!IS_OBJ(callee)) rt_error(vm, line, "a value of type %s is not callable", value_type_name(callee));
    Obj* o = callee.as.o;
    Bindings b;
    b.count = 0;
    switch (o->kind) {
        case OBJ_OVERLOAD: {
            Obj* t = resolve_overload(vm, (ObjOverload*)o, adl, argc, args, ic, &b, line);
            return invoke(vm, t, argc, args, &b, line);
        }
        case OBJ_FUNCTION:
        case OBJ_NATIVE:
        case OBJ_VARIANT_CTOR:
            if (!score_candidate(vm, o, argc, args, &b)) no_overload_error(vm, callable_sig(o)->name, &o, 1, argc, args, line);
            return invoke(vm, o, argc, args, &b, line);
        case OBJ_TYPEDEF: {
            /* Person({...}) checks the value against the type and tags it */
            ObjTypeDef* td = (ObjTypeDef*)o;
            if (argc != 1) rt_error(vm, line, "type '%s' takes exactly one value to convert", td->name->chars);
            Bindings nb;
            nb.count = td->ngenerics;
            for (int i = 0; i < td->ngenerics; i++) {
                memset(&nb.items[i], 0, sizeof(Binding));
                nb.items[i].name = td->generics[i];
            }
            if (!type_match(vm, td->def, args[0], td->env, &nb, true))
                rt_error(vm, line, "value of type %s does not match type '%s'", value_type_name(args[0]), td->name->chars);
            if (IS_KIND(args[0], OBJ_OBJECT) && td->def->kind == TY_RECORD) {
                AS_OBJECT(args[0])->tag = td;
                adopt_field_callbacks(vm, AS_OBJECT(args[0]));
            }
            return args[0];
        }
        default: rt_error(vm, line, "a value of type %s is not callable", value_type_name(callee));
    }
}

Value vm_call(VM* vm, Value callee, int argc, Value* args) {
    return call_value(vm, callee, argc, args, vm->cur_line, NULL);
}

/* ---------------------------------------------------------- member access */

static Value get_field(VM* vm, Value obj, ObjString* name, int line) {
    if (IS_KIND(obj, OBJ_OBJECT)) {
        Entry* e = table_find(&AS_OBJECT(obj)->fields, name);
        if (!e) rt_error(vm, line, "%s has no field '%s'", value_type_name(obj), name->chars);
        return e->value;
    }
    if (IS_KIND(obj, OBJ_MODULE)) {
        ObjModule* m = (ObjModule*)obj.as.o;
        Value v;
        if (!module_member(m, name, &v)) rt_error(vm, line, "module '%s' does not export '%s'", m->name->chars, name->chars);
        return v;
    }
    if (IS_KIND(obj, OBJ_ENUM)) {
        ObjEnum* en = (ObjEnum*)obj.as.o;
        for (int i = 0; i < en->nvariants; i++) {
            Variant* v = &en->variants[i];
            if (v->name != name && !str_equals(v->name, name->chars)) continue;
            if (v->is_ctor) return obj_val(v->ctor);
            return obj_val(v->singleton);
        }
        rt_error(vm, line, "enum '%s' has no variant '%s'", en->name->chars, name->chars);
    }
    rt_error(vm, line, "cannot access field '%s' on a value of type %s", name->chars, value_type_name(obj));
}

static int64_t norm_index(VM* vm, Value idx, int64_t count, int line) {
    if (idx.kind != VAL_INT) rt_error(vm, line, "index must be an int, got %s", value_type_name(idx));
    int64_t i = idx.as.i;
    if (i < 0) i += count;
    if (i < 0 || i >= count) rt_error(vm, line, "index %" PRId64 " out of range (length %" PRId64 ")", idx.as.i, count);
    return i;
}

static Value get_index(VM* vm, Value obj, Value idx, int line) {
    if (IS_KIND(obj, OBJ_ARRAY)) {
        ObjArray* a = AS_ARRAY(obj);
        return a->items[norm_index(vm, idx, a->count, line)];
    }
    if (IS_KIND(obj, OBJ_STRING)) {
        ObjString* s = AS_STR(obj);
        int64_t i = norm_index(vm, idx, s->len, line);
        return obj_val(str_new(vm, s->chars + i, 1));
    }
    if (IS_KIND(obj, OBJ_OBJECT)) {
        if (!IS_KIND(idx, OBJ_STRING)) rt_error(vm, line, "object keys must be strings, got %s", value_type_name(idx));
        Entry* e = table_find(&AS_OBJECT(obj)->fields, AS_STR(idx));
        if (!e) rt_error(vm, line, "%s has no field '%s'", value_type_name(obj), AS_STR(idx)->chars);
        return e->value;
    }
    rt_error(vm, line, "a value of type %s cannot be indexed", value_type_name(obj));
}

static Type* record_field_type(ObjTypeDef* td, ObjString* name, bool* found) {
    *found = false;
    if (!td || td->def->kind != TY_RECORD) return NULL;
    for (int i = 0; i < td->def->nfields; i++) {
        if (td->def->fnames[i] == name || str_equals(td->def->fnames[i], name->chars)) {
            *found = true;
            return td->def->ftypes[i];
        }
    }
    return NULL;
}

static void set_object_field(VM* vm, ObjObject* o, ObjString* key, Value v, int line) {
    if (o->tag) {
        bool found;
        Type* ft = record_field_type(o->tag, key, &found);
        if (!found) rt_error(vm, line, "type '%s' has no field '%s'", o->tag->name->chars, key->chars);
        char what[160];
        snprintf(what, sizeof what, "field '%s' of %s", key->chars, o->tag->name->chars);
        v = coerce_to(vm, ft, v, o->tag->env, NULL, true, what, line, key);
    }
    table_set(&o->fields, key, v);
}

static void set_index(VM* vm, Value obj, Value idx, Value v, int line) {
    if (IS_KIND(obj, OBJ_ARRAY)) {
        ObjArray* a = AS_ARRAY(obj);
        a->items[norm_index(vm, idx, a->count, line)] = v;
        return;
    }
    if (IS_KIND(obj, OBJ_OBJECT)) {
        if (!IS_KIND(idx, OBJ_STRING)) rt_error(vm, line, "object keys must be strings, got %s", value_type_name(idx));
        set_object_field(vm, AS_OBJECT(obj), AS_STR(idx), v, line);
        return;
    }
    rt_error(vm, line, "cannot assign by index into a value of type %s", value_type_name(obj));
}

/* --------------------------------------------------------- operator chains */

static double op_prec(VM* vm, ObjString* op) {
    Entry* e = table_find(&vm->prec, op);
    return e ? e->value.as.f : 65.0;
}

static int chain_build(Node* n, int* pos, double minp) {
    int left = -(*pos) - 1;
    while (*pos < n->as.chain.nops && n->as.chain.precs[*pos] >= minp) {
        int k = *pos;
        double p = n->as.chain.precs[k];
        (*pos)++;
        int right = chain_build(n, pos, p + 1e-9); /* left associative */
        n->as.chain.left[k] = left;
        n->as.chain.right[k] = right;
        left = k;
    }
    return left;
}

static void chain_rebuild(VM* vm, Node* n) {
    int nops = n->as.chain.nops;
    if (!n->as.chain.precs) {
        n->as.chain.precs = pp_malloc(sizeof(double) * (size_t)nops);
        n->as.chain.left = pp_malloc(sizeof(int) * (size_t)nops);
        n->as.chain.right = pp_malloc(sizeof(int) * (size_t)nops);
    }
    for (int k = 0; k < nops; k++) n->as.chain.precs[k] = op_prec(vm, n->as.chain.ops[k]);
    int pos = 0;
    n->as.chain.root = chain_build(n, &pos, -1e300);
    n->as.chain.version = vm->prec_version;
}

static bool require_bool(VM* vm, Value v, const char* what, int line) {
    if (v.kind != VAL_BOOL) rt_error(vm, line, "%s must be a bool, got %s", what, value_type_name(v));
    return v.as.b;
}

/* an operator in scope, or a global one */
static Value lookup_operator(VM* vm, ObjEnv* env, ObjString* op, int line) {
    Entry* e = env_find(env, op, NULL);
    if (e) return e->value;
    ObjOverload* g = global_set(vm, op);
    if (!g) rt_error(vm, line, "operator '%s' is not defined", op->chars);
    return obj_val(g);
}

static Value eval_chain_at(VM* vm, Node* n, ObjEnv* env, int enc) {
    if (enc < 0) return eval(vm, n->as.chain.operands.items[-enc - 1], env);
    int k = enc;
    ObjString* op = n->as.chain.ops[k];
    if (op == vm->s_and || op == vm->s_or) {
        bool is_and = op == vm->s_and;
        bool l = require_bool(vm, eval_chain_at(vm, n, env, n->as.chain.left[k]), "operand of '&&' / '||'", n->line);
        if (is_and && !l) return bool_val(false);
        if (!is_and && l) return bool_val(true);
        return bool_val(require_bool(vm, eval_chain_at(vm, n, env, n->as.chain.right[k]), "operand of '&&' / '||'", n->line));
    }
    int base = vm->sp;
    vm_push(vm, eval_chain_at(vm, n, env, n->as.chain.left[k]));
    vm_push(vm, eval_chain_at(vm, n, env, n->as.chain.right[k]));
    Value r = call_value(vm, lookup_operator(vm, env, op, n->line), 2, &vm->stack[base], n->line, &n->as.chain.ics[k]);
    vm->sp = base;
    return r;
}

/* ------------------------------------------------------------ expressions */

static Value eval_call_args(VM* vm, Node* n, ObjEnv* env, Value callee, NodeList* args, int base, InlineCache* ic) {
    int args_base = vm->sp;
    for (int i = 0; i < args->count; i++) vm_push(vm, eval(vm, args->items[i], env));
    Value r = call_value(vm, callee, vm->sp - args_base, &vm->stack[args_base], n->line, ic);
    vm->sp = base;
    return r;
}

static Value eval(VM* vm, Node* n, ObjEnv* env) {
    switch (n->kind) {
        case N_INT: return int_val(n->as.ival);
        case N_FLOAT: return float_val(n->as.fval);
        case N_STRING: return obj_val(n->as.str);
        case N_BOOL: return bool_val(n->as.bval);
        case N_IDENT: {
            Entry* e = env_find(env, n->as.str, NULL);
            if (!e) {
                ObjOverload* g = global_set(vm, n->as.str);
                if (g) return obj_val(g);
                rt_error(vm, n->line, "undefined name '%s'", n->as.str->chars);
            }
            if (e->flags & VAR_UNINIT) rt_error(vm, n->line, "variable '%s' is used before it was assigned", n->as.str->chars);
            return e->value;
        }
        case N_ARRAY: {
            NodeList* items = &n->as.array.items;
            ObjArray* a = array_new(vm, items->count);
            int base = vm->sp;
            vm_push(vm, obj_val(a));
            for (int i = 0; i < items->count; i++) {
                Value v = eval(vm, items->items[i], env);
                if (v.kind == VAL_VOID) rt_error(vm, items->items[i]->line, "array element has no value (void)");
                array_push(a, v);
            }
            vm->sp = base;
            return obj_val(a);
        }
        case N_OBJECT: {
            ObjObject* o = object_new(vm);
            int base = vm->sp;
            vm_push(vm, obj_val(o));
            for (int i = 0; i < n->as.object.values.count; i++) {
                Value v = eval(vm, n->as.object.values.items[i], env);
                if (v.kind == VAL_VOID) rt_error(vm, n->as.object.values.items[i]->line, "object field '%s' has no value (void)", n->as.object.keys[i]->chars);
                table_set(&o->fields, n->as.object.keys[i], v);
            }
            vm->sp = base;
            return obj_val(o);
        }
        case N_CALL: {
            int base = vm->sp;
            Value callee = eval(vm, n->as.call.callee, env);
            vm_push(vm, callee);
            return eval_call_args(vm, n, env, callee, &n->as.call.args, base, n->as.call.ic);
        }
        case N_METHOD: {
            int base = vm->sp;
            /* dot syntax: recv.f(args) == f(recv, args), recv.mod:f(args) == mod:f(recv, args) */
            Value recv = eval(vm, n->as.method.recv, env);
            vm_push(vm, recv);
            /* recv.f(args) also sees the functions exported by the module that declared recv's type */
            Node* cn = n->as.method.callee;
            ObjOverload* adl = NULL;
            Value callee;
            if (cn->kind == N_IDENT) {
                adl = receiver_module_set(vm, recv, cn->as.str);
                if (env_find(env, cn->as.str, NULL) || global_set(vm, cn->as.str)) callee = eval(vm, cn, env);
                else if (adl) callee = obj_val(adl);
                else rt_error(vm, n->line, "no function '%s' to call on a value of type %s", cn->as.str->chars, value_type_name(recv));
            } else {
                callee = eval(vm, cn, env);
            }
            vm_push(vm, callee);
            int args_base = vm->sp;
            vm_push(vm, recv);
            for (int i = 0; i < n->as.method.args.count; i++) vm_push(vm, eval(vm, n->as.method.args.items[i], env));
            Value r = call_value_adl(vm, callee, vm->sp - args_base, &vm->stack[args_base], n->line, n->as.method.ic, adl);
            vm->sp = base;
            return r;
        }
        case N_FIELD: return get_field(vm, eval(vm, n->as.field.obj, env), n->as.field.name, n->line);
        case N_INDEX: {
            int base = vm->sp;
            Value obj = eval(vm, n->as.index.obj, env);
            vm_push(vm, obj);
            Value idx = eval(vm, n->as.index.index, env);
            vm_push(vm, idx);
            Value r = get_index(vm, obj, idx, n->line);
            vm->sp = base;
            return r;
        }
        case N_CHAIN:
            if (n->as.chain.version != vm->prec_version) chain_rebuild(vm, n);
            return eval_chain_at(vm, n, env, n->as.chain.root);
        case N_FUNC: return obj_val(function_new(vm, n->as.func.sig, n->as.func.body, env));
        case N_IF:
        case N_MATCH: {
            Value out = void_val();
            ExecStatus st = exec_branchy(vm, n, env, &out);
            if (st != EX_NORMAL)
                rt_error(vm, n->line, "'break', 'continue' and 'return' cannot be used inside an if/match that is used as a value");
            return out;
        }
        default: rt_error(vm, n->line, "internal error: cannot evaluate node kind %d", n->kind);
    }
}

/* --------------------------------------------------------------- patterns */

static bool ctor_matches(Obj* c, ObjEnumValue* ev) {
    if (c->kind == OBJ_VARIANT_CTOR) {
        ObjVariantCtor* vc = (ObjVariantCtor*)c;
        return vc->en == ev->en && vc->index == ev->variant;
    }
    if (c->kind == OBJ_ENUM_VALUE) {
        ObjEnumValue* k = (ObjEnumValue*)c;
        return k->en == ev->en && k->variant == ev->variant;
    }
    return false;
}

static bool is_variantish(Value c) {
    if (IS_KIND(c, OBJ_VARIANT_CTOR) || IS_KIND(c, OBJ_ENUM_VALUE)) return true;
    if (IS_KIND(c, OBJ_OVERLOAD)) {
        ObjOverload* s = (ObjOverload*)c.as.o;
        for (int i = 0; i < s->count; i++)
            if (s->items[i]->kind == OBJ_VARIANT_CTOR) return true;
    }
    return false;
}

static bool variant_matches(Value c, Value v) {
    if (!IS_KIND(v, OBJ_ENUM_VALUE)) return false;
    ObjEnumValue* ev = AS_ENUMV(v);
    if (IS_KIND(c, OBJ_OVERLOAD)) {
        ObjOverload* s = (ObjOverload*)c.as.o;
        for (int i = 0; i < s->count; i++)
            if (ctor_matches(s->items[i], ev)) return true;
        return false;
    }
    return IS_OBJ(c) && ctor_matches(c.as.o, ev);
}

/* a payload-less constructor such as none() may be written without parens */
static bool is_unit_ctor(Value c) {
    if (IS_KIND(c, OBJ_VARIANT_CTOR)) {
        ObjVariantCtor* vc = (ObjVariantCtor*)c.as.o;
        return vc->en->variants[vc->index].npayload == 0;
    }
    if (IS_KIND(c, OBJ_OVERLOAD)) {
        ObjOverload* s = (ObjOverload*)c.as.o;
        for (int i = 0; i < s->count; i++) {
            Obj* o = s->items[i];
            if (o->kind == OBJ_VARIANT_CTOR && ((ObjVariantCtor*)o)->en->variants[((ObjVariantCtor*)o)->index].npayload == 0)
                return true;
        }
    }
    return false;
}

static bool match_pattern(VM* vm, Pattern* p, Value v, ObjEnv* env, uint8_t flags) {
    switch (p->kind) {
        case P_WILD: return true;
        case P_LITERAL: return values_equal(eval(vm, p->lit, env), v);
        case P_BIND: {
            Entry* e = env_find(env, p->name, NULL);
            if (e && is_unit_ctor(e->value)) return variant_matches(e->value, v);
            env_define(vm, env, p->name, v, flags, NULL);
            return true;
        }
        case P_VARIANT: {
            Value c = eval(vm, p->ctor, env);
            if (!is_variantish(c)) {
                if (p->has_parens) rt_error(vm, p->line, "pattern does not name an enum variant");
                return values_equal(c, v); /* qualified constant, e.g. Config.MAX */
            }
            if (!variant_matches(c, v)) return false;
            ObjEnumValue* ev = AS_ENUMV(v);
            if (p->has_parens) {
                if (ev->count != p->nsubs) return false;
                for (int i = 0; i < p->nsubs; i++)
                    if (!match_pattern(vm, p->subs[i], ev->payload[i], env, flags)) return false;
            }
            return true;
        }
    }
    return false;
}

static bool pattern_binds(Pattern* p) {
    if (p->kind == P_BIND) return true;
    for (int i = 0; i < p->nsubs; i++)
        if (pattern_binds(p->subs[i])) return true;
    return false;
}

/* ------------------------------------------------------------- statements */

static ExecStatus exec_branchy(VM* vm, Node* n, ObjEnv* env, Value* out) {
    *out = void_val();
    if (n->kind == N_IF) {
        if (n->as.if_.pat) {
            int base = vm->sp;
            Value v = eval(vm, n->as.if_.cond, env);
            vm_push(vm, v);
            ObjEnv* pe = env_new(vm, env);
            push_env(vm, pe);
            ExecStatus st = EX_NORMAL;
            bool matched = match_pattern(vm, n->as.if_.pat, v, pe, n->as.if_.pat_mut ? 0 : VAR_CONST);
            if (matched) st = exec_block(vm, n->as.if_.then_b, pe, true, out);
            pop_env(vm);
            vm->sp = base;
            if (matched) return st;
        } else {
            bool c = require_bool(vm, eval(vm, n->as.if_.cond, env), "if condition", n->line);
            if (c) return exec_block(vm, n->as.if_.then_b, env, true, out);
        }
        Node* e = n->as.if_.else_b;
        if (!e) return EX_NORMAL;
        if (e->kind == N_IF) return exec_branchy(vm, e, env, out);
        return exec_block(vm, e, env, true, out);
    }

    /* match */
    int base = vm->sp;
    Value v = eval(vm, n->as.match.subject, env);
    vm_push(vm, v);
    for (int i = 0; i < n->as.match.narms; i++) {
        MatchArm* arm = &n->as.match.arms[i];
        ObjEnv* ae = env;
        bool scoped = pattern_binds(arm->pat);
        if (scoped) {
            ae = env_new(vm, env);
            push_env(vm, ae);
        }
        bool ok = match_pattern(vm, arm->pat, v, ae, VAR_CONST);
        if (ok && arm->guard) ok = require_bool(vm, eval(vm, arm->guard, ae), "match guard", arm->guard->line);
        if (ok) {
            ExecStatus st = EX_NORMAL;
            if (arm->body_is_block) st = exec_block(vm, arm->body, ae, true, out);
            else *out = eval(vm, arm->body, ae);
            if (scoped) pop_env(vm);
            vm->sp = base;
            return st;
        }
        if (scoped) pop_env(vm);
    }
    StrBuf sb;
    sb_init(&sb);
    value_format(&sb, v, false, 0);
    rt_error(vm, n->line, "no match arm matched the value %s", sb.data);
}

static void hoist_functions(VM* vm, Node* b, ObjEnv* env) {
    NodeList* sl = &b->as.block.stmts;
    for (int i = 0; i < sl->count; i++) {
        Node* s = sl->items[i];
        if (s->kind != S_FUNCDECL) continue;
        vm->cur_line = s->line;
        ObjFunction* fn = function_new(vm, s->as.func.sig, s->as.func.body, env);
        env_add_function(vm, env, s->as.func.sig->name, (Obj*)fn);
        if (s->as.func.global) global_add(vm, s->as.func.sig->name, (Obj*)fn);
    }
}

static ExecStatus exec_block(VM* vm, Node* b, ObjEnv* env, bool new_scope, Value* out) {
    bool pushed = false;
    if (new_scope && b->as.block.scoped) {
        env = env_new(vm, env);
        push_env(vm, env);
        pushed = true;
    }
    hoist_functions(vm, b, env);
    if (out) *out = void_val();
    ExecStatus st = EX_NORMAL;
    NodeList* sl = &b->as.block.stmts;
    for (int i = 0; i < sl->count; i++) {
        Node* s = sl->items[i];
        if (out && i == sl->count - 1 && s->kind == S_EXPR) {
            /* last expression of a block used as a value */
            vm->cur_line = s->line;
            gc_maybe(vm);
            Node* e = s->as.expr.expr;
            if (e->kind == N_IF || e->kind == N_MATCH) st = exec_branchy(vm, e, env, out);
            else *out = eval(vm, e, env);
        } else {
            st = exec(vm, s, env);
        }
        if (st != EX_NORMAL) break;
    }
    if (pushed) pop_env(vm);
    return st;
}

static Value apply_op(VM* vm, ObjEnv* env, ObjString* op, Value a, Value b, int line, InlineCache* ic) {
    int base = vm->sp;
    vm_push(vm, a);
    vm_push(vm, b);
    Value r = call_value(vm, lookup_operator(vm, env, op, line), 2, &vm->stack[base], line, ic);
    vm->sp = base;
    return r;
}

static void exec_assign(VM* vm, Node* n, ObjEnv* env) {
    Node* target = n->as.assign.target;
    ObjString* op = n->as.assign.op;
    int base = vm->sp;
    Value rhs = eval(vm, n->as.assign.value, env);
    if (rhs.kind == VAL_VOID) rt_error(vm, n->line, "cannot assign a void value");
    vm_push(vm, rhs);
    switch (target->kind) {
        case N_IDENT: {
            ObjString* name = target->as.str;
            ObjEnv* where;
            Entry* e = env_find(env, name, &where);
            if (!e) rt_error(vm, n->line, "assignment to undeclared variable '%s' (declare it with 'let')", name->chars);
            if (e->flags & VAR_CONST) rt_error(vm, n->line, "cannot assign to constant '%s'", name->chars);
            Value v = rhs;
            if (op) {
                if (e->flags & VAR_UNINIT) rt_error(vm, n->line, "variable '%s' is used before it was assigned", name->chars);
                v = apply_op(vm, env, op, e->value, rhs, n->line, n->as.assign.ic);
                e = env_find(env, name, &where);
            }
            if (e->type) {
                char what[160];
                snprintf(what, sizeof what, "cannot assign to '%s'", name->chars);
                v = coerce_to(vm, e->type, v, where, NULL, true, what, n->line, name);
                e = env_find(env, name, &where);
            }
            e->value = v;
            e->flags &= (uint8_t)~VAR_UNINIT;
            break;
        }
        case N_FIELD: {
            Value obj = eval(vm, target->as.field.obj, env);
            vm_push(vm, obj);
            if (!IS_KIND(obj, OBJ_OBJECT))
                rt_error(vm, n->line, "cannot assign field '%s' on a value of type %s", target->as.field.name->chars, value_type_name(obj));
            Value v = rhs;
            if (op) v = apply_op(vm, env, op, get_field(vm, obj, target->as.field.name, n->line), rhs, n->line, n->as.assign.ic);
            set_object_field(vm, AS_OBJECT(obj), target->as.field.name, v, n->line);
            break;
        }
        case N_INDEX: {
            Value obj = eval(vm, target->as.index.obj, env);
            vm_push(vm, obj);
            Value idx = eval(vm, target->as.index.index, env);
            vm_push(vm, idx);
            Value v = rhs;
            if (op) v = apply_op(vm, env, op, get_index(vm, obj, idx, n->line), rhs, n->line, n->as.assign.ic);
            set_index(vm, obj, idx, v, n->line);
            break;
        }
        default: rt_error(vm, n->line, "invalid assignment target");
    }
    vm->sp = base;
}

static void exec_let(VM* vm, Node* n, ObjEnv* env) {
    uint8_t flags = n->as.let.is_const ? VAR_CONST : 0;
    Type* type = n->as.let.type;
    Value v;
    if (n->as.let.init) {
        v = eval(vm, n->as.let.init, env);
        if (v.kind == VAL_VOID) rt_error(vm, n->line, "cannot initialize '%s' with a void value", n->as.let.name->chars);
        if (type) {
            char what[160];
            snprintf(what, sizeof what, "cannot initialize '%s'", n->as.let.name->chars);
            v = coerce_to(vm, type, v, env, NULL, true, what, n->line, n->as.let.name);
        }
    } else if (n->as.let.is_const) {
        v = bool_val(true); /* `const NAME` - a define flag */
    } else {
        v = void_val();
        flags |= VAR_UNINIT;
    }
    env_define(vm, env, n->as.let.name, v, flags, type);
}

static void exec_enum(VM* vm, Node* n, ObjEnv* env) {
    ObjEnum* en = (ObjEnum*)alloc_obj(vm, sizeof(ObjEnum), OBJ_ENUM);
    en->name = n->as.enum_decl.name;
    en->generics = n->as.enum_decl.generics;
    en->defaults = n->as.enum_decl.defaults;
    en->ngenerics = n->as.enum_decl.ngenerics;
    en->value_type = n->as.enum_decl.value_type;
    en->env = env;
    en->module = module_of_env(vm, env);
    int nv = n->as.enum_decl.nvariants;
    en->variants = pp_calloc((size_t)(nv ? nv : 1), sizeof(Variant));
    env_define(vm, env, en->name, obj_val(en), VAR_CONST, NULL);
    for (int i = 0; i < nv; i++) {
        VariantDecl* d = &n->as.enum_decl.variants[i];
        Variant* var = &en->variants[i];
        Value value = int_val(i);
        if (d->value) {
            value = eval(vm, d->value, env);
            if (en->value_type) {
                char what[160];
                snprintf(what, sizeof what, "value of %s.%s", en->name->chars, d->name->chars);
                value = coerce_to(vm, en->value_type, value, env, NULL, true, what, d->value->line, NULL);
            }
        } else if (en->value_type && !(en->value_type->kind == TY_PRIM && en->value_type->prim == PRIM_INT)) {
            rt_error(vm, n->line, "variant '%s' of enum '%s' needs a value", d->name->chars, en->name->chars);
        }
        var->name = d->name;
        var->is_ctor = d->is_ctor;
        var->payload = d->payload;
        var->npayload = d->npayload;
        var->value = value;
        if (d->npayload == 0) var->singleton = enumv_new(vm, en, i, 0);
        if (d->is_ctor) {
            ObjVariantCtor* c = (ObjVariantCtor*)alloc_obj(vm, sizeof(ObjVariantCtor), OBJ_VARIANT_CTOR);
            c->en = en;
            c->index = i;
            Signature* sig = pp_calloc(1, sizeof(Signature));
            sig->name = d->name;
            sig->generics = en->generics;
            sig->ngenerics = en->ngenerics;
            sig->nparams = d->npayload;
            sig->params = pp_calloc((size_t)(d->npayload ? d->npayload : 1), sizeof(Param));
            for (int k = 0; k < d->npayload; k++) {
                char pname[16];
                snprintf(pname, sizeof pname, "_%d", k);
                sig->params[k].name = str_cintern(vm, pname);
                sig->params[k].type = d->payload[k];
            }
            sig->file = vm->cur_file;
            sig->line = n->line;
            c->sig = sig;
            var->ctor = c;
        }
        en->nvariants = i + 1;
        if (d->is_ctor) env_add_function(vm, env, d->name, (Obj*)var->ctor);
    }
}

/* ------------------------------------------------------------- interfaces */

static void exec_interface(VM* vm, Node* n, ObjEnv* env) {
    ObjString* name = n->as.iface.name;
    Entry* prev = table_find(&env->vars, name);
    if (prev) {
        if (IS_KIND(prev->value, OBJ_INTERFACE))
            rt_error(vm, n->line,
                     "interface '%s' is already declared (an interface is closed: list all of its functions in one declaration)",
                     name->chars);
        rt_error(vm, n->line, "cannot declare interface '%s': the name is already used by a %s value", name->chars,
                 value_type_name(prev->value));
    }
    ObjInterface* it = (ObjInterface*)alloc_obj(vm, sizeof(ObjInterface), OBJ_INTERFACE);
    it->name = name;
    it->generics = n->as.iface.generics;
    it->ngenerics = n->as.iface.ngenerics;
    it->reqs = n->as.iface.reqs;
    it->nreqs = n->as.iface.nreqs;
    it->env = env;
    it->module = module_of_env(vm, env);
    env_define(vm, env, name, obj_val(it), VAR_CONST, NULL);
}

/* str(Box) -> string: a requirement with the interface name replaced by the implementing type */
static void requirement_to_string(Signature* sig, ObjString* self, const char* target, StrBuf* sb) {
    sb_printf(sb, "%s(", sig->name->chars);
    for (int i = 0; i < sig->nparams; i++) {
        if (i) sb_puts(sb, ", ");
        Type* t = sig->params[i].type;
        if (t && t->kind == TY_NAME && !t->nquals && t->name == self) sb_puts(sb, target);
        else type_to_string(t, sb);
    }
    if (sig->variadic) sb_puts(sb, sig->nparams ? ", ..." : "...");
    sb_putc(sb, ')');
    if (sig->ret) {
        sb_puts(sb, " -> ");
        type_to_string(sig->ret, sb);
    }
}

static void add_binding(VM* vm, Bindings* b, ObjString* name, Type* type, ObjEnv* env, int line) {
    if (b->count >= PP_MAX_BINDINGS) rt_error(vm, line, "too many generic parameters (max %d)", PP_MAX_BINDINGS);
    Binding* bd = &b->items[b->count++];
    memset(bd, 0, sizeof *bd);
    bd->name = name;
    bd->type = type;
    bd->env = env;
}

/* the module that declared a named type, for looking up its exported functions */
static ObjModule* type_module(Value tv) {
    if (IS_KIND(tv, OBJ_TYPEDEF)) return ((ObjTypeDef*)tv.as.o)->module;
    if (IS_KIND(tv, OBJ_ENUM)) return ((ObjEnum*)tv.as.o)->module;
    return NULL;
}

/*
 * impl Iface for Type: every function the interface lists must exist right
 * now, written for Type (a catch-all such as the built-in str(any) does not
 * count). Functions are looked up where the impl is written, among global
 * functions, and among the exports of the module that declared Type.
 */
static void exec_impl(VM* vm, Node* n, ObjEnv* env) {
    Type* it_t = n->as.impl.iface;
    Type* target = n->as.impl.target;
    Value iv;
    if (it_t->kind != TY_NAME || !resolve_type_value(vm, it_t, env, &iv)) {
        StrBuf sb;
        sb_init(&sb);
        type_to_string(it_t, &sb);
        rt_error(vm, n->line, "unknown interface '%s'", sb.data);
    }
    if (!IS_KIND(iv, OBJ_INTERFACE)) rt_error(vm, n->line, "'%s' is a %s, not an interface", it_t->name->chars, value_type_name(iv));
    ObjInterface* it = (ObjInterface*)iv.as.o;
    if (it_t->nargs != it->ngenerics)
        rt_error(vm, n->line, "interface '%s' takes %d type argument(s) but %d were given", it->name->chars, it->ngenerics,
                 it_t->nargs);

    StrBuf tname;
    sb_init(&tname);
    type_to_string(target, &tname);
    Value tv = void_val();
    if (target->kind == TY_NAME) {
        if (!resolve_type_value(vm, target, env, &tv)) rt_error(vm, n->line, "unknown type '%s'", tname.data);
        if (IS_KIND(tv, OBJ_INTERFACE))
            rt_error(vm, n->line, "'%s' is an interface; only types can implement an interface", tname.data);
        if (!IS_KIND(tv, OBJ_TYPEDEF) && !IS_KIND(tv, OBJ_ENUM)) rt_error(vm, n->line, "'%s' is not a type", tname.data);
        for (int i = 0; i < it->nimpls; i++) {
            Value ov;
            Impl* o = &it->impls[i];
            if (o->target->kind == TY_NAME && resolve_type_value(vm, o->target, o->env, &ov) && ov.as.o == tv.as.o)
                rt_error(vm, n->line, "type '%s' already implements interface '%s'", tname.data, it->name->chars);
        }
    }
    ObjModule* tmod = type_module(tv);

    for (int r = 0; r < it->nreqs; r++) {
        Signature* req = it->reqs[r];
        Bindings b;
        b.count = 0;
        for (int k = 0; k < it->ngenerics; k++) add_binding(vm, &b, it->generics[k], it_t->args[k], env, n->line);
        add_binding(vm, &b, it->name, target, env, n->line); /* the interface name means the implementing type */
        for (int k = 0; k < req->ngenerics; k++) add_binding(vm, &b, req->generics[k], NULL, NULL, n->line);

        Type** ptypes = pp_malloc(sizeof(Type*) * (size_t)(req->nparams ? req->nparams : 1));
        for (int k = 0; k < req->nparams; k++) ptypes[k] = req->params[k].type;

        Value cands[3];
        int ncands = 0;
        Entry* e = env_find(env, req->name, NULL);
        if (e) cands[ncands++] = e->value;
        ObjOverload* g = global_set(vm, req->name);
        if (g) cands[ncands++] = obj_val(g);
        Value mv;
        if (tmod && module_member(tmod, req->name, &mv)) cands[ncands++] = mv;

        bool ok = false;
        for (int k = 0; k < ncands && !ok; k++) {
            if (!is_callable(cands[k])) continue;
            ok = callable_fits(vm, cands[k].as.o, ptypes, req->nparams, req->variadic, req->ret, it->env, &b, it->name, true);
        }
        free(ptypes);
        if (!ok) {
            StrBuf sb;
            sb_init(&sb);
            requirement_to_string(req, it->name, tname.data, &sb);
            rt_error(vm, n->line, "type '%s' does not implement interface '%s': missing function %s", tname.data, it->name->chars,
                     sb.data);
        }
    }

    GROW(it->impls, it->nimpls, it->capimpls);
    Impl* im = &it->impls[it->nimpls++];
    im->target = target;
    im->env = env;
    im->args = it_t->args;
    im->nargs = it_t->nargs;
    sb_free(&tname);
    /* overload choices cached at call sites may change now that values of the type fit the interface */
    for (int i = 0; i < vm->ncaches; i++) vm->caches[i]->sets[0] = NULL;
}

static void exec_precedence(VM* vm, Node* n) {
    ObjString* op = n->as.prec.op;
    int rel = n->as.prec.rel;
    double other = op_prec(vm, n->as.prec.other);
    double cur = op_prec(vm, op);
    double np = other;
    if (rel > 0 && cur > other) return; /* already binds tighter */
    if (rel < 0 && cur < other) return;
    if (rel != 0) {
        /* move between `other` and the next distinct level in that direction */
        double best = rel > 0 ? other + 10 : other - 10;
        double cands[1] = {65.0};
        for (int i = -1; i < vm->prec.count; i++) {
            double q;
            if (i < 0) q = cands[0];
            else {
                if (vm->prec.entries[i].key == op) continue;
                q = vm->prec.entries[i].value.as.f;
            }
            if (rel > 0 && q > other && q < best) best = q;
            if (rel < 0 && q < other && q > best) best = q;
        }
        np = (other + best) / 2;
    }
    table_set(&vm->prec, op, float_val(np));
    vm->prec_version++;
}

static void exec_export(VM* vm, Node* n);

static void bind_import(VM* vm, ObjEnv* env, ObjString* name, Value v) {
    if (IS_KIND(v, OBJ_OVERLOAD)) {
        ObjOverload* s = (ObjOverload*)v.as.o;
        for (int i = 0; i < s->count; i++) env_add_function(vm, env, name, s->items[i]);
        return;
    }
    env_define(vm, env, name, v, VAR_CONST, NULL);
}

static ObjModule* load_module_opt(VM* vm, const char* path, bool is_file, int line, bool required);

static void export_name(VM* vm, int line, ObjString* name, ObjString* alias) {
    ObjModule* m = vm->cur_module;
    if (!m) rt_error(vm, line, "'export' can only be used in a module file");
    Entry* prev = table_find(&m->exports, alias);
    if (prev && AS_STR(prev->value) != name) rt_error(vm, line, "'%s' is already exported", alias->chars);
    module_export(m, name, alias);
}

/*
 * import os:fs - the export `fs` of module os, or else the file os/fs.pp next to
 * where os would be. The first segment may be missing (a plain directory).
 */
static ObjModule* import_target(VM* vm, Node* n) {
    int nsubs = n->as.import.nsubs;
    ObjModule* m = load_module_opt(vm, n->as.import.path->chars, n->as.import.is_file, n->line, nsubs == 0);
    if (nsubs == 0) return m;
    StrBuf path, shown;
    sb_init(&path);
    sb_init(&shown);
    sb_puts(&path, n->as.import.path->chars);
    sb_printf(&shown, n->as.import.is_file ? "\"%s\"" : "%s", n->as.import.path->chars);
    for (int i = 0; i < nsubs; i++) {
        ObjString* seg = n->as.import.subs[i];
        sb_printf(&path, "/%s", seg->chars);
        Value v;
        if (m && module_member(m, seg, &v)) {
            if (!IS_KIND(v, OBJ_MODULE))
                rt_error(vm, n->line, "%s:%s is a %s, not a module (use import %s -> { %s })", shown.data, seg->chars,
                         value_type_name(v), shown.data, seg->chars);
            m = (ObjModule*)v.as.o;
        } else {
            ObjModule* sub = load_module_opt(vm, path.data, n->as.import.is_file, n->line, false);
            if (!sub)
                rt_error(vm, n->line, "cannot find module %s:%s (%s, and there is no file %s.pp)", shown.data, seg->chars,
                         m ? "it is not exported by its parent" : "the parent module does not exist", path.data);
            m = sub;
        }
        sb_printf(&shown, ":%s", seg->chars);
    }
    sb_free(&path);
    sb_free(&shown);
    return m;
}

static void exec_import(VM* vm, Node* n, ObjEnv* env) {
    ObjModule* m = import_target(vm, n);
    vm->cur_line = n->line;
    if (n->as.import.destructure) {
        for (int i = 0; i < n->as.import.nnames; i++) {
            ObjString* name = n->as.import.names[i];
            Value v;
            if (!module_member(m, name, &v)) rt_error(vm, n->line, "module '%s' does not export '%s'", m->name->chars, name->chars);
            bind_import(vm, env, name, v);
        }
    } else {
        env_define(vm, env, n->as.import.alias, obj_val(m), VAR_CONST, NULL);
    }
    if (n->as.import.reexport) {
        if (n->as.import.destructure)
            for (int i = 0; i < n->as.import.nnames; i++) export_name(vm, n->line, n->as.import.names[i], n->as.import.names[i]);
        else
            export_name(vm, n->line, n->as.import.alias, n->as.import.alias);
    }
}

static ExecStatus run_iteration(VM* vm, Node* n, ObjEnv* env, Value a, Value b, uint8_t flags) {
    ObjEnv* ie = env_new(vm, env);
    push_env(vm, ie);
    if (n->as.for_.var2) {
        env_define(vm, ie, n->as.for_.var1, a, flags, NULL);
        env_define(vm, ie, n->as.for_.var2, b, flags, NULL);
    } else {
        env_define(vm, ie, n->as.for_.var1, b, flags, NULL);
    }
    ExecStatus st = exec_block(vm, n->as.for_.body, ie, false, NULL);
    pop_env(vm);
    return st;
}

static int utf8_len(unsigned char c) {
    if (c < 0x80) return 1;
    if ((c >> 5) == 0x6) return 2;
    if ((c >> 4) == 0xE) return 3;
    if ((c >> 3) == 0x1E) return 4;
    return 1;
}

static ExecStatus exec_for(VM* vm, Node* n, ObjEnv* env) {
    int base = vm->sp;
    Value it = eval(vm, n->as.for_.iter, env);
    vm_push(vm, it);
    uint8_t flags = n->as.for_.mut ? 0 : VAR_CONST;
    ExecStatus result = EX_NORMAL;
    ExecStatus st;
#define HANDLE_STATUS()                                                                                                \
    if (st == EX_BREAK) break;                                                                                         \
    if (st == EX_RETURN) {                                                                                             \
        result = EX_RETURN;                                                                                            \
        break;                                                                                                         \
    }
    if (IS_KIND(it, OBJ_RANGE)) {
        ObjRange* r = AS_RANGE(it);
        int64_t idx = 0;
        for (int64_t i = r->start; r->step > 0 ? i < r->end : i > r->end; i += r->step, idx++) {
            st = run_iteration(vm, n, env, int_val(idx), int_val(i), flags);
            HANDLE_STATUS()
        }
    } else if (IS_KIND(it, OBJ_ARRAY)) {
        ObjArray* a = AS_ARRAY(it);
        for (int i = 0; i < a->count; i++) {
            st = run_iteration(vm, n, env, int_val(i), a->items[i], flags);
            HANDLE_STATUS()
        }
    } else if (IS_KIND(it, OBJ_STRING)) {
        ObjString* s = AS_STR(it);
        int64_t idx = 0;
        for (int i = 0; i < s->len; idx++) {
            int len = utf8_len((unsigned char)s->chars[i]);
            if (i + len > s->len) len = s->len - i;
            Value ch = obj_val(str_new(vm, s->chars + i, len));
            i += len;
            st = run_iteration(vm, n, env, int_val(idx), ch, flags);
            HANDLE_STATUS()
        }
    } else if (IS_KIND(it, OBJ_OBJECT)) {
        ObjObject* o = AS_OBJECT(it);
        for (int i = 0; i < o->fields.count; i++) {
            Entry* e = &o->fields.entries[i];
            if (n->as.for_.var2) st = run_iteration(vm, n, env, obj_val(e->key), e->value, flags);
            else st = run_iteration(vm, n, env, void_val(), obj_val(e->key), flags);
            HANDLE_STATUS()
        }
    } else {
        rt_error(vm, n->line, "cannot iterate over a value of type %s", value_type_name(it));
    }
#undef HANDLE_STATUS
    vm->sp = base;
    return result;
}

static ExecStatus exec(VM* vm, Node* n, ObjEnv* env) {
    vm->cur_line = n->line;
    gc_maybe(vm);
    switch (n->kind) {
        case S_EXPR: {
            Node* e = n->as.expr.expr;
            if (e->kind == N_IF || e->kind == N_MATCH) {
                Value out;
                return exec_branchy(vm, e, env, &out);
            }
            eval(vm, e, env);
            return EX_NORMAL;
        }
        case S_LET: exec_let(vm, n, env); return EX_NORMAL;
        case S_ASSIGN: exec_assign(vm, n, env); return EX_NORMAL;
        case S_FUNCDECL: return EX_NORMAL; /* hoisted by exec_block */
        case S_TYPE: {
            ObjTypeDef* td = (ObjTypeDef*)alloc_obj(vm, sizeof(ObjTypeDef), OBJ_TYPEDEF);
            td->name = n->as.type_decl.name;
            td->generics = n->as.type_decl.generics;
            td->ngenerics = n->as.type_decl.ngenerics;
            td->def = n->as.type_decl.def;
            td->env = env;
            td->module = module_of_env(vm, env);
            env_define(vm, env, td->name, obj_val(td), VAR_CONST, NULL);
            return EX_NORMAL;
        }
        case S_ENUM: exec_enum(vm, n, env); return EX_NORMAL;
        case S_IMPORT: exec_import(vm, n, env); return EX_NORMAL;
        case S_EXPORT: exec_export(vm, n); return EX_NORMAL;
        case S_INTERFACE: exec_interface(vm, n, env); return EX_NORMAL;
        case S_IMPL: exec_impl(vm, n, env); return EX_NORMAL;
        case S_PRECEDENCE: exec_precedence(vm, n); return EX_NORMAL;
        case S_FOR: return exec_for(vm, n, env);
        case S_WHILE:
            for (;;) {
                vm->cur_line = n->line;
                if (!require_bool(vm, eval(vm, n->as.while_.cond, env), "while condition", n->line)) break;
                ExecStatus st = exec_block(vm, n->as.while_.body, env, true, NULL);
                if (st == EX_BREAK) break;
                if (st == EX_RETURN) return EX_RETURN;
            }
            return EX_NORMAL;
        case S_BREAK: return EX_BREAK;
        case S_CONTINUE: return EX_CONTINUE;
        case S_RETURN:
            vm->ret = n->as.ret.value ? eval(vm, n->as.ret.value, env) : void_val();
            return EX_RETURN;
        case N_BLOCK: return exec_block(vm, n, env, true, NULL);
        default: eval(vm, n, env); return EX_NORMAL;
    }
}

/* ---------------------------------------------------------------- modules */

static bool file_exists(const char* path) {
    FILE* f = fopen(path, "rb");
    if (!f) return false;
    fclose(f);
    return true;
}

static char* absolute_path(const char* path) {
#ifdef _WIN32
    char buf[MAX_PATH * 4];
    if (_fullpath(buf, path, sizeof buf)) return pp_strdup(buf);
#else
    char buf[PATH_MAX];
    if (realpath(path, buf)) return pp_strdup(buf);
#endif
    return pp_strdup(path);
}

static char* dir_of(const char* file) {
    const char* slash = NULL;
    for (const char* c = file; *c; c++)
        if (*c == '/' || *c == '\\') slash = c;
    if (!slash) return pp_strdup(".");
    return pp_strndup(file, (size_t)(slash - file));
}

static bool is_absolute(const char* p) {
    if (p[0] == '/' || p[0] == '\\') return true;
    return isalpha((unsigned char)p[0]) && p[1] == ':';
}

static ObjString* module_key(VM* vm, const char* abs) {
    char* k = pp_strdup(abs);
#ifdef _WIN32
    for (char* c = k; *c; c++) {
        *c = (char)tolower((unsigned char)*c);
        if (*c == '/') *c = '\\';
    }
#endif
    ObjString* s = str_cnew(vm, k);
    free(k);
    return s;
}

static ExecStatus run_program(VM* vm, Node* program, ObjModule* m, ObjEnv* env, const char* file, Value* out) {
    const char* saved_file = vm->cur_file;
    int saved_line = vm->cur_line;
    ObjModule* saved_module = vm->cur_module;
    vm->cur_file = file;
    vm->cur_module = m;
    push_env(vm, env);
    ExecStatus st = exec_block(vm, program, env, false, out);
    pop_env(vm);
    if (m) {
        /* exports may name symbols defined after the export line; check them once the module ran */
        for (int i = 0; i < m->exports.count; i++) {
            ObjString* src = AS_STR(m->exports.entries[i].value);
            if (!table_find(&m->env->vars, src))
                rt_error(vm, 0, "'%s' is exported but never defined at the top level of the module", src->chars);
        }
    }
    vm->cur_module = saved_module;
    vm->cur_file = saved_file;
    vm->cur_line = saved_line;
    return st;
}

static void exec_export(VM* vm, Node* n) {
    for (int i = 0; i < n->as.export_.count; i++) export_name(vm, n->line, n->as.export_.names[i], n->as.export_.aliases[i]);
}

/*
 * import name     - native module `name`, or name.pp in a library directory
 * import "./path" - a file relative to the importing file (.pp is optional)
 */
/* required = false: returns NULL instead of failing when no such module exists */
static ObjModule* load_module_opt(VM* vm, const char* path, bool is_file, int line, bool required) {
    if (!is_file) {
        for (int i = 0; i < vm->nnative_modules; i++) {
            if (strcmp(vm->native_modules[i].name, path) != 0) continue;
            StrBuf key;
            sb_init(&key);
            sb_printf(&key, "native:%s", path);
            Entry* e = table_find_chars(&vm->modules, key.data, (int)key.len, hash_chars(key.data, (int)key.len));
            if (e) {
                sb_free(&key);
                return (ObjModule*)e->value.as.o;
            }
            ObjModule* m = vm->native_modules[i].init(vm);
            table_set(&vm->modules, str_new(vm, key.data, (int)key.len), obj_val(m));
            sb_free(&key);
            return m;
        }
    }

    size_t plen = strlen(path);
    bool has_ext = plen > 3 && strcmp(path + plen - 3, ".pp") == 0;
    char** dirs = pp_malloc(sizeof(char*) * (size_t)(vm->nsearch + 1));
    int ndirs = 0;
    if (is_file) {
        if (!is_absolute(path)) dirs[ndirs++] = (vm->cur_file && vm->cur_file[0] != '<') ? dir_of(vm->cur_file) : pp_strdup(".");
    } else {
        for (int i = 0; i < vm->nsearch; i++) dirs[ndirs++] = pp_strdup(vm->search_paths[i]);
    }
    char* found = NULL;
    StrBuf tried;
    sb_init(&tried);
    if (is_file && is_absolute(path)) {
        StrBuf c;
        sb_init(&c);
        sb_printf(&c, "%s%s", path, has_ext ? "" : ".pp");
        if (file_exists(c.data)) found = pp_strdup(c.data);
        else sb_printf(&tried, "\n    %s", c.data);
        sb_free(&c);
    }
    for (int i = 0; i < ndirs && !found; i++) {
        StrBuf c;
        sb_init(&c);
        sb_printf(&c, "%s/%s%s", dirs[i], path, has_ext ? "" : ".pp");
#ifdef _WIN32
        for (size_t k = 0; k < c.len; k++)
            if (c.data[k] == '/') c.data[k] = '\\';
#endif
        if (file_exists(c.data)) found = pp_strdup(c.data);
        else sb_printf(&tried, "\n    %s", c.data);
        sb_free(&c);
    }
    for (int i = 0; i < ndirs; i++) free(dirs[i]);
    free(dirs);
    if (!found && !required) {
        sb_free(&tried);
        return NULL;
    }
    if (!found) {
        if (is_file) rt_error(vm, line, "cannot find file \"%s\", tried:%s", path, tried.data);
        rt_error(vm, line, "cannot find module '%s'%s%s%s", path, ndirs ? ", tried:" : "", tried.data,
                 "\n  (files are imported with a path string: import \"./file\")");
    }
    sb_free(&tried);

    char* abs = absolute_path(found);
    free(found);
    ObjString* key = module_key(vm, abs);
    Entry* cached = table_find(&vm->modules, key);
    if (cached) {
        free(abs);
        return (ObjModule*)cached->value.as.o;
    }

    size_t len;
    char* src = pp_read_file(abs, &len);
    if (!src) rt_error(vm, line, "cannot read module '%s'", abs);
    Node* program = parse_program(vm, src, abs);
    if (!program) rt_error(vm, line, "module '%s' failed to parse", path);

    const char* base = abs;
    for (const char* c = abs; *c; c++)
        if (*c == '/' || *c == '\\') base = c + 1;
    int blen = (int)strlen(base);
    if (blen > 3 && strcmp(base + blen - 3, ".pp") == 0) blen -= 3;

    char* name = pp_strndup(base, (size_t)blen);
    ObjModule* m = module_new(vm, name, abs, vm->builtins);
    free(name);
    table_set(&vm->modules, key, obj_val(m)); /* cached before running: cyclic imports see a partial module */
    run_program(vm, program, m, m->env, abs, NULL);
    return m;
}

/* ----------------------------------------------------------------- entry */

static void vm_reset(VM* vm) {
    vm->sp = 0;
    vm->esp = 0;
    vm->fp = 0;
    vm->gc_disabled = 0;
    vm->ret = void_val();
    vm->cur_module = NULL;
}

int vm_run_file(VM* vm, const char* path) {
    jmp_buf jb;
    jmp_buf* prev = vm->err_jmp;
    vm->err_jmp = &jb;
    if (setjmp(jb)) {
        vm->err_jmp = prev;
        vm_reset(vm);
        return 1;
    }
    char* abs = absolute_path(path);
    size_t len;
    char* src = pp_read_file(abs, &len);
    if (!src) {
        fprintf(stderr, "error: cannot open file '%s'\n", path);
        vm->err_jmp = prev;
        return 66;
    }
    Node* program = parse_program(vm, src, abs);
    if (!program) {
        vm->err_jmp = prev;
        return 65;
    }
    ObjModule* m = module_new(vm, "__main__", abs, vm->builtins);
    table_set(&vm->modules, module_key(vm, abs), obj_val(m));
    run_program(vm, program, m, m->env, abs, NULL);
    fflush(stdout);
    vm->err_jmp = prev;
    return 0;
}

int vm_exec_repl(VM* vm, const char* src, ObjEnv* env) {
    jmp_buf jb;
    jmp_buf* prev = vm->err_jmp;
    vm->err_jmp = &jb;
    if (setjmp(jb)) {
        vm->err_jmp = prev;
        vm_reset(vm);
        return 1;
    }
    Node* program = parse_program(vm, pp_strdup(src), "<repl>");
    if (!program) {
        vm->err_jmp = prev;
        return 1;
    }
    Value out = void_val();
    ExecStatus st = run_program(vm, program, NULL, env, "<repl>", &out);
    if (st == EX_NORMAL && out.kind != VAL_VOID) {
        StrBuf sb;
        sb_init(&sb);
        value_format(&sb, out, false, 0);
        printf("%s\n", sb.data);
        sb_free(&sb);
    }
    fflush(stdout);
    vm->err_jmp = prev;
    return 0;
}

static char* executable_dir(const char* argv0) {
#ifdef _WIN32
    char buf[MAX_PATH * 4];
    DWORD n = GetModuleFileNameA(NULL, buf, sizeof buf);
    if (n > 0 && n < sizeof buf) return dir_of(buf);
#endif
    char* abs = absolute_path(argv0 ? argv0 : ".");
    char* d = dir_of(abs);
    free(abs);
    return d;
}

static void add_search_path(VM* vm, const char* p) {
    vm->search_paths = pp_realloc(vm->search_paths, sizeof(char*) * (size_t)(vm->nsearch + 1));
    vm->search_paths[vm->nsearch++] = pp_strdup(p);
}

VM* vm_new(int argc, char** argv) {
    VM* vm = pp_calloc(1, sizeof(VM));
    vm->stack = pp_malloc(sizeof(Value) * PP_STACK_MAX);
    vm->envs = pp_malloc(sizeof(ObjEnv*) * PP_ENV_STACK_MAX);
    vm->frames = pp_malloc(sizeof(Frame) * PP_FRAMES_MAX);
    vm->next_gc = 200000;
    vm->gc_disabled = 1;
    vm->argc = argc;
    vm->argv = argv;
    table_init(&vm->interned);
    table_init(&vm->modules);
    table_init(&vm->globals);
    table_init(&vm->prec);
    vm->ret = void_val();
    vm->s_and = str_cintern(vm, "&&");
    vm->s_or = str_cintern(vm, "||");
    vm->s_wild = str_cintern(vm, "_");

    static const struct {
        const char* op;
        double prec;
    } precs[] = {
        {"||", 10}, {"&&", 20}, {"==", 30}, {"!=", 30}, {"<", 40}, {">", 40}, {"<=", 40}, {">=", 40},
        {"...", 50}, {"...=", 50}, {"+", 60}, {"-", 60}, {"*", 70}, {"/", 70}, {"%", 70}, {"**", 80},
        {NULL, 0},
    };
    for (int i = 0; precs[i].op; i++) table_set(&vm->prec, str_cintern(vm, precs[i].op), float_val(precs[i].prec));

    vm->builtins = env_new(vm, NULL);
    builtins_register(vm);
    mod_io_register(vm);
    mod_os_register(vm);

    jmp_buf jb;
    vm->err_jmp = &jb;
    if (setjmp(jb)) {
        fprintf(stderr, "fatal: the prelude failed to load\n");
        exit(70);
    }
    Node* prelude = parse_program(vm, PP_PRELUDE, "<prelude>");
    if (!prelude) {
        fprintf(stderr, "fatal: the prelude failed to parse\n");
        exit(70);
    }
    run_program(vm, prelude, NULL, vm->builtins, "<prelude>", NULL);
    vm->err_jmp = NULL;

    Entry* opt = table_find(&vm->builtins->vars, str_cintern(vm, "Option"));
    Entry* err = table_find(&vm->builtins->vars, str_cintern(vm, "Error"));
    vm->option_enum = (ObjEnum*)opt->value.as.o;
    vm->error_enum = (ObjEnum*)err->value.as.o;

    char* exe_dir = executable_dir(argc > 0 ? argv[0] : NULL);
    StrBuf lib;
    sb_init(&lib);
    sb_printf(&lib, "%s/lib", exe_dir);
    add_search_path(vm, lib.data);
    sb_free(&lib);
    free(exe_dir);
    const char* env_path = getenv("PP_PATH");
    if (env_path) {
#ifdef _WIN32
        const char sep = ';';
#else
        const char sep = ':';
#endif
        const char* s = env_path;
        while (*s) {
            const char* e = strchr(s, sep);
            size_t n = e ? (size_t)(e - s) : strlen(s);
            if (n > 0) {
                char* part = pp_strndup(s, n);
                add_search_path(vm, part);
                free(part);
            }
            if (!e) break;
            s = e + 1;
        }
    }

    vm->cur_file = NULL;
    vm->cur_line = 0;
    vm->gc_disabled = 0;
    return vm;
}
