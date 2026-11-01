/*
 * Mark & sweep garbage collector.
 *
 * Collection only happens at statement boundaries (gc_maybe is called before
 * each statement executes). Every value that is alive across the execution of
 * a statement must therefore be reachable from a root: the value stack, the
 * scope stack, the builtins scope or the module cache.
 */
#include "vm.h"

static Obj** gray;
static int ngray, capgray;

static void mark_obj(Obj* o) {
    if (!o || o->marked || o->permanent) return;
    o->marked = true;
    GROW(gray, ngray, capgray);
    gray[ngray++] = o;
}

static void mark_value(Value v) {
    if (v.kind == VAL_OBJ) mark_obj(v.as.o);
}

static void mark_table(Table* t) {
    for (int i = 0; i < t->count; i++) {
        mark_obj((Obj*)t->entries[i].key);
        mark_value(t->entries[i].value);
    }
}

static void blacken(Obj* o) {
    switch (o->kind) {
        case OBJ_STRING:
        case OBJ_RANGE:
        case OBJ_NATIVE: break;
        case OBJ_ARRAY: {
            ObjArray* a = (ObjArray*)o;
            for (int i = 0; i < a->count; i++) mark_value(a->items[i]);
            break;
        }
        case OBJ_OBJECT: {
            ObjObject* ob = (ObjObject*)o;
            mark_table(&ob->fields);
            mark_obj((Obj*)ob->tag);
            break;
        }
        case OBJ_FUNCTION: mark_obj((Obj*)((ObjFunction*)o)->closure); break;
        case OBJ_OVERLOAD: {
            ObjOverload* s = (ObjOverload*)o;
            for (int i = 0; i < s->count; i++) mark_obj(s->items[i]);
            break;
        }
        case OBJ_ENUM: {
            ObjEnum* en = (ObjEnum*)o;
            mark_obj((Obj*)en->env);
            mark_obj((Obj*)en->module);
            mark_table(&en->impl_fns);
            for (int i = 0; i < en->nvariants; i++) {
                mark_value(en->variants[i].value);
                mark_obj((Obj*)en->variants[i].singleton);
                mark_obj((Obj*)en->variants[i].ctor);
            }
            break;
        }
        case OBJ_VARIANT_CTOR: mark_obj((Obj*)((ObjVariantCtor*)o)->en); break;
        case OBJ_ENUM_VALUE: {
            ObjEnumValue* ev = (ObjEnumValue*)o;
            mark_obj((Obj*)ev->en);
            for (int i = 0; i < ev->count; i++) mark_value(ev->payload[i]);
            break;
        }
        case OBJ_TYPEDEF:
            mark_obj((Obj*)((ObjTypeDef*)o)->env);
            mark_obj((Obj*)((ObjTypeDef*)o)->module);
            mark_table(&((ObjTypeDef*)o)->impl_fns);
            break;
        case OBJ_INTERFACE: {
            ObjInterface* it = (ObjInterface*)o;
            mark_obj((Obj*)it->env);
            mark_obj((Obj*)it->module);
            for (int i = 0; i < it->nimpls; i++) mark_obj((Obj*)it->impls[i].env);
            break;
        }
        case OBJ_MODULE: {
            ObjModule* m = (ObjModule*)o;
            mark_obj((Obj*)m->name);
            mark_obj((Obj*)m->path);
            mark_obj((Obj*)m->env);
            mark_table(&m->exports);
            break;
        }
        case OBJ_ENV: {
            ObjEnv* e = (ObjEnv*)o;
            mark_obj((Obj*)e->parent);
            mark_table(&e->vars);
            break;
        }
    }
}

static void free_obj(Obj* o) {
    switch (o->kind) {
        case OBJ_ARRAY: free(((ObjArray*)o)->items); break;
        case OBJ_OBJECT: table_free(&((ObjObject*)o)->fields); break;
        case OBJ_OVERLOAD: free(((ObjOverload*)o)->items); break;
        case OBJ_ENUM:
            free(((ObjEnum*)o)->variants);
            table_free(&((ObjEnum*)o)->impl_fns);
            break;
        case OBJ_TYPEDEF: table_free(&((ObjTypeDef*)o)->impl_fns); break;
        case OBJ_ENV: table_free(&((ObjEnv*)o)->vars); break;
        case OBJ_MODULE: table_free(&((ObjModule*)o)->exports); break;
        case OBJ_INTERFACE: free(((ObjInterface*)o)->impls); break;
        case OBJ_FUNCTION:
            if (((ObjFunction*)o)->owns_sig) {
                free(((ObjFunction*)o)->sig->params);
                free(((ObjFunction*)o)->sig);
            }
            break;
        default: break;
    }
    free(o);
}

void gc_collect(VM* vm) {
    /* inline caches hold raw pointers; they must never outlive their targets */
    for (int i = 0; i < vm->ncaches; i++) vm->caches[i]->sets[0] = NULL;

    for (int i = 0; i < vm->sp; i++) mark_value(vm->stack[i]);
    for (int i = 0; i < vm->esp; i++) mark_obj((Obj*)vm->envs[i]);
    mark_obj((Obj*)vm->builtins);
    mark_table(&vm->modules);
    mark_table(&vm->globals);
    mark_value(vm->ret);
    mark_obj((Obj*)vm->option_enum);
    mark_obj((Obj*)vm->error_enum);
    for (int i = 0; i < vm->nctypes; i++) mark_obj(vm->ctypes[i]->direct);

    while (ngray > 0) blacken(gray[--ngray]);

    size_t live = 0;
    Obj** link = &vm->objects;
    while (*link) {
        Obj* o = *link;
        if (o->marked) {
            o->marked = false;
            link = &o->next;
            live++;
        } else {
            *link = o->next;
            free_obj(o);
        }
    }
    vm->num_objects = live;
    vm->next_gc = live * 2 > 200000 ? live * 2 : 200000;
}

void gc_maybe(VM* vm) {
    if (vm->gc_disabled == 0 && vm->num_objects >= vm->next_gc) gc_collect(vm);
}
