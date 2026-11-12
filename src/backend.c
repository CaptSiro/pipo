#include "backend.h"



// What both compiling targets need to know about a function body before compiling it.



bool names_has(const NameList* l, ObjString* name) {
    for (int i = 0; i < l->count; ++i) {
        if (l->items[i] == name || str_equals(l->items[i], name->chars)) {
            return true;
        }
    }

    return false;
}

void names_add(NameList* l, ObjString* name) {
    GROW(l->items, l->count, l->cap);
    l->items[l->count++] = name;
}

static void used_list(NameList* out, NodeList* l, bool nested) {
    for (int i = 0; i < l->count; ++i) {
        names_used_nested(out, l->items[i], nested);
    }
}

static void used_pattern(NameList* out, Pattern* p, bool nested) {
    if (!p) {
        return;
    }

    if (p->lit) {
        names_used_nested(out, p->lit, nested);
    }

    if (p->ctor) {
        names_used_nested(out, p->ctor, nested);
    }

    for (int i = 0; i < p->nsubs; ++i) {
        used_pattern(out, p->subs[i], nested);
    }
}

void names_used_nested(NameList* out, Node* n, bool nested) {
    if (!n) {
        return;
    }

    switch (n->kind) {
        case N_IDENT:
            if (nested && !names_has(out, n->as.str)) {
                names_add(out, n->as.str);
            }

            return;
        case N_ARRAY: used_list(out, &n->as.array.items, nested); return;
        case N_OBJECT: used_list(out, &n->as.object.values, nested); return;
        case N_CALL:
            names_used_nested(out, n->as.call.callee, nested);
            used_list(out, &n->as.call.args, nested);

            return;
        case N_METHOD:
            names_used_nested(out, n->as.method.recv, nested);
            names_used_nested(out, n->as.method.callee, nested);
            used_list(out, &n->as.method.args, nested);

            return;
        case N_FIELD: names_used_nested(out, n->as.field.obj, nested); return;
        case N_INDEX:
            names_used_nested(out, n->as.index.obj, nested);
            used_list(out, &n->as.index.args, nested);

            return;
        case N_CHAIN: used_list(out, &n->as.chain.operands, nested); return;
        case N_FUNC:
        case S_FUNCDECL: names_used_nested(out, n->as.func.body, true); return;
        case N_IF:
            names_used_nested(out, n->as.if_.cond, nested);
            used_pattern(out, n->as.if_.pat, nested);
            names_used_nested(out, n->as.if_.then_b, nested);
            names_used_nested(out, n->as.if_.else_b, nested);

            return;
        case N_MATCH:
            names_used_nested(out, n->as.match.subject, nested);
            for (int i = 0; i < n->as.match.narms; ++i) {
                used_pattern(out, n->as.match.arms[i].pat, nested);
                names_used_nested(out, n->as.match.arms[i].guard, nested);
                names_used_nested(out, n->as.match.arms[i].body, nested);
            }

            return;
        case N_BLOCK: used_list(out, &n->as.block.stmts, nested); return;
        case N_SCOPECALL:
            used_list(out, &n->as.scall.args, nested);
            names_used_nested(out, n->as.scall.body, true);

            return;
        case N_AS: names_used_nested(out, n->as.as_.expr, nested); return;
        case S_EXPR: names_used_nested(out, n->as.expr.expr, nested); return;
        case S_LET: names_used_nested(out, n->as.let.init, nested); return;
        case S_ASSIGN:
            names_used_nested(out, n->as.assign.target, nested);
            names_used_nested(out, n->as.assign.value, nested);

            return;
        case S_FOR:
            names_used_nested(out, n->as.for_.iter, nested);
            names_used_nested(out, n->as.for_.body, nested);

            return;
        case S_WHILE:
            names_used_nested(out, n->as.while_.cond, nested);
            names_used_nested(out, n->as.while_.body, nested);

            return;
        case S_RETURN: names_used_nested(out, n->as.ret.value, nested); return;
        case S_EXPORT: names_used_nested(out, n->as.export_.decl, nested); return;
        case S_DEFER: names_used_nested(out, n->as.defer.body, true); return;
        default: return;
    }
}
