#include "check.h"



// interfaces and callables need the checker to decide whether a type fits; check.c sets these
bool (*st_implements_hook)(TDecl* iface, SType** args, int nargs, SType* got) = NULL;
bool (*st_callable_fits_hook)(SType* want, SType* got) = NULL;
SType* (*st_fill_hook)(SType* t) = NULL;



SType* st_new(STKind kind) {
    SType* t = pp_calloc(1, sizeof(SType));
    t->kind = kind;

    return t;
}

SType* st_prim(STKind kind) {
    static SType prims[ST_BLOCK + 1];
    prims[kind].kind = kind;

    return &prims[kind];
}

SType* st_array(SType* elem) {
    SType* t = st_new(ST_ARRAY);
    t->elem = elem;

    return t;
}

static void union_add(SType* u, int* cap, SType* m) {
    if (m->kind == ST_UNION) {
        for (int i = 0; i < m->nargs; ++i) {
            union_add(u, cap, m->args[i]);
        }

        return;
    }

    if (m->kind == ST_NEVER) {
        return;
    }

    for (int i = 0; i < u->nargs; ++i) {
        if (st_equal(u->args[i], m)) {
            return;
        }
    }

    GROW(u->args, u->nargs, *cap);
    u->args[u->nargs++] = m;
}

SType* st_union(SType* a, SType* b) {
    if (a->kind == ST_DYN || b->kind == ST_DYN) {
        return st_prim(ST_DYN);
    }

    if (a->kind == ST_UNKNOWN || b->kind == ST_UNKNOWN) {
        return st_prim(ST_UNKNOWN);
    }

    SType* u = st_new(ST_UNION);
    int cap = 0;
    union_add(u, &cap, a);
    union_add(u, &cap, b);
    if (u->nargs == 0) {
        return st_prim(ST_NEVER);
    }

    if (u->nargs == 1) {
        return u->args[0];
    }

    return u;
}

static bool union_has(const SType* u, const SType* m) {
    for (int i = 0; i < u->nargs; ++i) {
        if (st_equal(u->args[i], m)) {
            return true;
        }
    }

    return false;
}

bool st_equal(const SType* a, const SType* b) {
    if (a == b) {
        return true;
    }

    if (!a || !b || a->kind != b->kind) {
        return false;
    }

    switch (a->kind) {
        case ST_ARRAY: return st_equal(a->elem, b->elem);
        case ST_RANGE: return (!a->elem && !b->elem) || (a->elem && b->elem && a->elem->kind == b->elem->kind);
        case ST_TUPLE:
        case ST_ENUM:
        case ST_NATIVE:
        case ST_IFACE:
            if (a->decl != b->decl || a->nargs != b->nargs) {
                return false;
            }

            for (int i = 0; i < a->nargs; ++i) {
                if (!st_equal(a->args[i], b->args[i])) {
                    return false;
                }
            }

            return true;
        case ST_UNION:
            if (a->nargs != b->nargs) {
                return false;
            }

            for (int i = 0; i < a->nargs; ++i) {
                if (!union_has(b, a->args[i])) {
                    return false;
                }
            }

            return true;
        case ST_RECORD:
            if (a->decl || b->decl) {
                if (a->decl != b->decl || a->nargs != b->nargs) {
                    return false;
                }

                for (int i = 0; i < a->nargs; ++i) {
                    if (!st_equal(a->args[i], b->args[i])) {
                        return false;
                    }
                }

                return true;
            }

            if (a->nfields != b->nfields) {
                return false;
            }

            for (int i = 0; i < a->nfields; ++i) {
                if (a->fnames[i] != b->fnames[i] || !st_equal(a->ftypes[i], b->ftypes[i])) {
                    return false;
                }
            }

            return true;
        case ST_FUNC:
            if (a->nargs != b->nargs || a->variadic != b->variadic || !st_equal(a->elem, b->elem)) {
                return false;
            }

            for (int i = 0; i < a->nargs; ++i) {
                if (!st_equal(a->args[i], b->args[i])) {
                    return false;
                }
            }

            return true;
        case ST_PARAM: return a->name == b->name;
        case ST_OVERLOAD: return a->funcs == b->funcs;
        case ST_MODULE: return a->module == b->module;
        case ST_TYPE: return a->decl == b->decl && st_equal(a->elem, b->elem);
        case ST_BLOCK:
            return a->variadic == b->variadic && st_equal(a->elem, b->elem) && a->nargs == b->nargs &&
                   (!a->nargs || st_equal(a->args[0], b->args[0]));
        default: return true;
    }
}

bool st_is_dyn(const SType* t) {
    if (!t) {
        return false;
    }

    switch (t->kind) {
        case ST_DYN: return true;
        case ST_ARRAY:
        case ST_BLOCK: return st_is_dyn(t->elem);
        case ST_FUNC:
            if (t->elem && st_is_dyn(t->elem)) {
                return true;
            }
            // fallthrough
        case ST_TUPLE:
        case ST_ENUM:
        case ST_NATIVE:
        case ST_IFACE:
        case ST_UNION:
            for (int i = 0; i < t->nargs; ++i) {
                if (st_is_dyn(t->args[i])) {
                    return true;
                }
            }

            return false;
        case ST_RECORD:
            for (int i = 0; i < t->nfields; ++i) {
                if (st_is_dyn(t->ftypes[i])) {
                    return true;
                }
            }

            return false;
        default: return false;
    }
}

bool st_is_numeric(const SType* t) {
    if (t->kind == ST_INT || t->kind == ST_FLOAT) {
        return true;
    }

    if (t->kind != ST_UNION) {
        return false;
    }

    for (int i = 0; i < t->nargs; ++i) {
        if (t->args[i]->kind != ST_INT && t->args[i]->kind != ST_FLOAT) {
            return false;
        }
    }

    return true;
}

static void print_args(SType** args, int n, StrBuf* sb) {
    for (int i = 0; i < n; ++i) {
        if (i) {
            sb_puts(sb, ", ");
        }

        st_print(args[i], sb);
    }
}

void st_print(const SType* t, StrBuf* sb) {
    if (!t) {
        sb_puts(sb, "unknown");

        return;
    }

    if (t->alias && t->kind != ST_UNION) {
        sb_puts(sb, t->alias->chars);
        if (t->kind == ST_FUNC && t->nargs && st_has_param(t)) {
            sb_putc(sb, '<');
            bool first = true;
            for (int i = 0; i < t->nargs; ++i) {
                if (t->args[i]->kind == ST_PARAM) {
                    sb_printf(sb, first ? "%s" : ", %s", t->args[i]->name->chars);
                    first = false;
                }
            }

            sb_putc(sb, '>');
        }

        return;
    }

    switch (t->kind) {
        case ST_DYN: sb_puts(sb, "dynamic"); return;
        case ST_NEVER: sb_puts(sb, "never"); return;
        case ST_UNKNOWN: sb_puts(sb, "unknown"); return;
        case ST_VOID: sb_puts(sb, "void"); return;
        case ST_BOOL: sb_puts(sb, "bool"); return;
        case ST_INT: sb_puts(sb, "int"); return;
        case ST_FLOAT: sb_puts(sb, "float"); return;
        case ST_STRING: sb_puts(sb, "string"); return;
        case ST_RANGE:
            sb_puts(sb, "range");
            if (t->elem) {
                sb_printf(sb, "<%s>", t->elem->kind == ST_FLOAT ? "float" : "int");
            }

            return;
        case ST_OBJECT: sb_puts(sb, "object"); return;
        case ST_FUNCTION: sb_puts(sb, "function"); return;
        case ST_ARRAY:
            if (t->elem->kind == ST_UNION || t->elem->kind == ST_FUNC) {
                sb_putc(sb, '(');
                st_print(t->elem, sb);
                sb_putc(sb, ')');
            } else {
                st_print(t->elem, sb);
            }

            sb_puts(sb, "[]");

            return;
        case ST_TUPLE:
            sb_puts(sb, "tuple<");
            print_args(t->args, t->nargs, sb);
            sb_putc(sb, '>');

            return;
        case ST_RECORD:
            if (t->decl) {
                sb_puts(sb, t->decl->name->chars);
                if (t->nargs) {
                    sb_putc(sb, '<');
                    print_args(t->args, t->nargs, sb);
                    sb_putc(sb, '>');
                }

                return;
            }

            sb_puts(sb, "{ ");
            for (int i = 0; i < t->nfields; ++i) {
                if (i) {
                    sb_puts(sb, ", ");
                }

                st_print(t->ftypes[i], sb);
                sb_printf(sb, " %s", t->fnames[i]->chars);
            }

            sb_puts(sb, t->nfields ? " }" : "}");

            return;
        case ST_ENUM:
        case ST_NATIVE:
        case ST_IFACE:
            if (t->kind == ST_ENUM && t->decl->name && str_equals(t->decl->name, "Option") && t->nargs == 1) {
                st_print(t->args[0], sb);
                sb_putc(sb, '?');

                return;
            }

            sb_puts(sb, t->decl->name->chars);
            if (t->nargs) {
                sb_putc(sb, '<');
                print_args(t->args, t->nargs, sb);
                sb_putc(sb, '>');
            }

            return;
        case ST_FUNC:
            sb_putc(sb, '(');
            if (t->variadic) {
                sb_puts(sb, "...");
            } else {
                print_args(t->args, t->nargs, sb);
            }

            sb_puts(sb, ") -> ");
            st_print(t->elem, sb);

            return;
        case ST_UNION:
            for (int i = 0; i < t->nargs; ++i) {
                if (i) {
                    sb_puts(sb, " || ");
                }

                st_print(t->args[i], sb);
            }

            return;
        case ST_PARAM: sb_puts(sb, t->name->chars); return;
        case ST_OVERLOAD: sb_puts(sb, "function"); return;
        case ST_MODULE: sb_puts(sb, "module"); return;
        case ST_TYPE: sb_puts(sb, "type"); return;
        case ST_BLOCK:
            sb_puts(sb, t->variadic ? "Callback<" : "Block<");
            st_print(t->elem, sb);
            sb_putc(sb, '>');

            return;
    }
}

const char* st_name(const SType* t) {
    static StrBuf sb;
    if (!sb.data) {
        sb_init(&sb);
    }

    sb.len = 0;
    sb_puts(&sb, "");
    st_print(t, &sb);

    return sb.data;
}

// ---------------------------------------------------------------- fitting

static int min_score(int a, int b) {
    return a < b ? a : b;
}

static SType* binding_of(SBindings* b, ObjString* name, int* index) {
    for (int i = 0; b && i < b->count; ++i) {
        if (b->names[i] == name) {
            *index = i;

            return b->types[i];
        }
    }

    *index = -1;

    return NULL;
}

static int fit_record(SType* want, SType* got, SBindings* b) {
    if (got->kind != ST_RECORD) {
        return 0;
    }

    if (st_fill_hook) {
        st_fill_hook(want);
        st_fill_hook(got);
    }

    if (want->decl && got->decl == want->decl) {
        for (int i = 0; i < want->nargs && i < got->nargs; ++i) {
            if (!st_fit(want->args[i], got->args[i], b)) {
                return 0;
            }
        }

        return 5;
    }

    for (int i = 0; i < want->nfields; ++i) {
        int k = -1;
        for (int j = 0; j < got->nfields; ++j) {
            if (got->fnames[j] == want->fnames[i]) {
                k = j;
                break;
            }
        }

        if (k < 0 || !st_fit(want->ftypes[i], got->ftypes[k], b)) {
            return 0;
        }
    }

    return 3;
}

bool st_has_param(const SType* t) {
    if (!t) {
        return false;
    }

    switch (t->kind) {
        case ST_PARAM: return true;
        case ST_ARRAY:
        case ST_BLOCK: return st_has_param(t->elem);
        case ST_FUNC:
            if (st_has_param(t->elem)) {
                return true;
            }
            // fallthrough
        case ST_TUPLE:
        case ST_ENUM:
        case ST_NATIVE:
        case ST_IFACE:
        case ST_UNION:
            for (int i = 0; i < t->nargs; ++i) {
                if (st_has_param(t->args[i])) {
                    return true;
                }
            }

            return false;
        case ST_RECORD:
            for (int i = 0; i < t->nargs; ++i) {
                if (st_has_param(t->args[i])) {
                    return true;
                }
            }

            for (int i = 0; i < t->nfields; ++i) {
                if (st_has_param(t->ftypes[i])) {
                    return true;
                }
            }

            return false;
        default: return false;
    }
}

// a function of type got where one of type want is expected: its parameters must accept
// want's (they bind want's generics), and its result must fit want's unless want ignores it
static int fit_func(SType* want, SType* got, SBindings* b) {
    if (want->variadic || got->variadic) {
        return 3;
    }

    if (want->nargs != got->nargs) {
        return 0;
    }

    for (int i = 0; i < want->nargs; ++i) {
        const int s = st_has_param(want->args[i]) ? st_fit(want->args[i], got->args[i], b)
                                                  : st_fit(got->args[i], want->args[i], NULL);
        if (!s) {
            return 0;
        }
    }

    const SType* wr = want->elem;
    if (!wr || wr->kind == ST_VOID || wr->kind == ST_UNKNOWN || !got->elem) {
        return 3;
    }

    return st_fit(want->elem, got->elem, b) ? 3 : 0;
}

int st_fit(SType* want, SType* got, SBindings* b) {
    if (!want || want->kind == ST_UNKNOWN) {
        return got && got->kind == ST_VOID ? 0 : 1;
    }

    if (want->kind == ST_DYN || got->kind == ST_DYN) {
        return 1;
    }

    if (got->kind == ST_NEVER) {
        return 4;
    }

    if (want->kind == ST_PARAM) {
        int index;
        SType* bound = binding_of(b, want->name, &index);
        if (index < 0) {
            return got->kind == ST_VOID ? 0 : 1; // a free generic accepts anything
        }

        if (got->kind == ST_PARAM && got->name == want->name) {
            return 2; // T where T is expected: in a generic body, before it is bound
        }

        if (bound && !(bound->kind == ST_ARRAY && bound->elem->kind == ST_NEVER)) {
            // bound by an earlier argument; a float after an int widens the binding, like the runtime's number
            const int s = st_fit(bound, got, NULL);
            if (s) {
                return 2;
            }

            if (st_is_numeric(bound) && st_is_numeric(got)) {
                b->types[index] = st_union(bound, got);

                return 2;
            }

            return 0;
        }

        if (got->kind == ST_VOID) {
            return 0;
        }

        b->types[index] = got;

        return 2;
    }

    if (got->kind == ST_UNION && want->kind != ST_UNION) {
        // every member has to fit
        int best = 5;
        for (int i = 0; i < got->nargs; ++i) {
            const int s = st_fit(want, got->args[i], b);
            if (!s) {
                return 0;
            }

            best = min_score(best, s);
        }

        return best;
    }

    switch (want->kind) {
        case ST_VOID:
        case ST_BOOL:
        case ST_INT:
        case ST_STRING: return got->kind == want->kind ? 4 : 0;
        case ST_RANGE:
            if (got->kind != ST_RANGE) {
                return 0;
            }

            return !want->elem || !got->elem || want->elem->kind == got->elem->kind ? 4 : 0;
        case ST_FLOAT: return got->kind == ST_FLOAT ? 4 : got->kind == ST_INT ? 2 : 0;
        case ST_FUNCTION: return got->kind == ST_FUNC || got->kind == ST_OVERLOAD || got->kind == ST_FUNCTION ? 3 : 0;
        case ST_OBJECT: return got->kind == ST_RECORD || got->kind == ST_OBJECT ? 2 : 0;
        case ST_TYPE: return got->kind == ST_TYPE ? 3 : 0;
        case ST_ARRAY: {
            if (got->kind != ST_ARRAY) {
                return 0;
            }

            if (got->elem->kind == ST_NEVER) {
                return 3; // the empty array []
            }

            const int s = st_fit(want->elem, got->elem, b);

            return s ? 2 + s : 0;
        }
        case ST_TUPLE: {
            if (got->kind != ST_TUPLE || got->nargs != want->nargs) {
                return 0;
            }

            int best = 5;
            for (int i = 0; i < want->nargs; ++i) {
                const int s = st_fit(want->args[i], got->args[i], b);
                if (!s) {
                    return 0;
                }

                best = min_score(best, s);
            }

            return 2 + best;
        }
        case ST_UNION: {
            if (got->kind == ST_UNION) {
                int best = 5;
                for (int i = 0; i < got->nargs; ++i) {
                    const int s = st_fit(want, got->args[i], b);
                    if (!s) {
                        return 0;
                    }

                    best = min_score(best, s);
                }

                return best;
            }

            int best = 0;
            for (int i = 0; i < want->nargs; ++i) {
                const int s = st_fit(want->args[i], got, b);
                if (s > best) {
                    best = s;
                }
            }

            return best > 1 ? best - 1 : best;
        }
        case ST_RECORD: return fit_record(want, got, b);
        case ST_ENUM: {
            if (got->kind != ST_ENUM || got->decl != want->decl) {
                return 0;
            }

            for (int i = 0; i < want->nargs && i < got->nargs; ++i) {
                if (!st_fit(want->args[i], got->args[i], b)) {
                    return 0;
                }
            }

            return 4;
        }
        case ST_NATIVE: {
            if (got->kind != ST_NATIVE || got->decl != want->decl) {
                return 0;
            }

            if (want->decl->which == NT_CLASS) {
                return 5;
            }

            if (!want->nargs || !got->nargs) {
                return 4;
            }

            int best = 5;
            for (int i = 0; i < want->nargs && i < got->nargs; ++i) {
                const int s = st_fit(want->args[i], got->args[i], b);
                if (!s) {
                    return 0;
                }

                best = min_score(best, s);
            }

            return 2 + best;
        }
        case ST_IFACE:
            if (got->kind == ST_IFACE && got->decl == want->decl) {
                for (int i = 0; i < want->nargs && i < got->nargs; ++i) {
                    if (!st_fit(want->args[i], got->args[i], b)) {
                        return 0;
                    }
                }

                return 3;
            }

            return st_implements_hook && st_implements_hook(want->decl, want->args, want->nargs, got) ? 2 : 0;
        case ST_FUNC:
            if (got->kind == ST_FUNCTION) {
                if (got->arity && !want->variadic && want->nargs != got->arity - 1) {
                    return 0;
                }

                return 3;
            }

            if (got->kind == ST_FUNC) {
                return fit_func(want, got, b);
            }

            if (got->kind != ST_OVERLOAD) {
                return 0;
            }

            return st_callable_fits_hook && st_callable_fits_hook(want, got) ? 3 : 0;
        case ST_BLOCK: return got->kind == ST_BLOCK ? 5 : 0;
        case ST_MODULE: return got->kind == ST_MODULE && got->module == want->module ? 4 : 0;
        default: return 0;
    }
}
