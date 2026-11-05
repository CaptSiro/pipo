/*
 * Internal (native) functions. Every native is registered with a signature
 * written in the language's own syntax, so natives take part in overload
 * resolution exactly like user functions do:
 *
 *     def(vm, "+", "(number a, number b)", n_add);
 *
 * User code can add overloads to any of these names, e.g.
 *     function +(Vec a, Vec b) -> Vec { ... }
 */
#include <errno.h>
#include <math.h>
#include <time.h>

#include "parser.h"
#include "vm.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

void def_native(VM* vm, ObjEnv* env, const char* name, const char* sig, NativeFn fn) {
    Signature* s = parse_native_signature(vm, name, sig);
    ObjNative* nf = native_new(vm, s, fn);
    env_add_function(vm, env, s->name, (Obj*)nf);
}

/*
 * Constants exposed by native modules (or the builtins scope):
 *
 *     def_const_str(vm, m->env, "nl", "\n");      // io:nl
 *     def_const(vm, m->env, "max_int", int_val(INT64_MAX));
 *
 * They are read-only bindings, so `io:nl = "x"` is rejected like any const.
 * The value is kept on the VM stack while the name is interned so a GC
 * triggered by that allocation cannot collect it.
 */
void def_const(VM* vm, ObjEnv* env, const char* name, Value v) {
    vm_push(vm, v);
    ObjString* s = str_cintern(vm, name);
    env_define(vm, env, s, v, VAR_CONST, NULL);
    vm->sp--;
}

void def_const_str(VM* vm, ObjEnv* env, const char* name, const char* value) {
    def_const(vm, env, name, obj_val(str_cnew(vm, value)));
}

/* a native module exports everything it defines */
void module_export_all(ObjModule* m) {
    for (int i = 0; i < m->env->vars.count; i++) {
        module_export(m, m->env->vars.entries[i].key, m->env->vars.entries[i].key);
    }
}

/*
 * A native module inside a native module, reached as parent:name and
 * importable as `import parent:name`. Call it from the parent's init while the
 * parent is on the VM stack:
 *
 *     def_submodule(vm, m, "fs", fs_init);      // os:fs
 */
void def_submodule(VM* vm, ObjModule* parent, const char* name, NativeModuleInit init) {
    ObjModule* child = init(vm);
    def_const(vm, parent->env, name, obj_val(child));
}

static void def(VM* vm, const char* name, const char* sig, NativeFn fn) {
    def_native(vm, vm->builtins, name, sig, fn);
}

static double num(Value v) {
    return v.kind == VAL_INT ? (double)v.as.i : v.as.f;
}

static Value new_string(VM* vm, const char* s, int len) {
    return obj_val(str_new(vm, s, len));
}

static ObjString* to_string(VM* vm, Value v) {
    if (IS_KIND(v, OBJ_STRING)) {
        return AS_STR(v);
    }

    StrBuf sb;
    sb_init(&sb);
    value_format(&sb, v, true, 0);
    ObjString* s = str_new(vm, sb.data, (int)sb.len);
    sb_free(&sb);

    return s;
}

/* ------------------------------------------------------------- arithmetic */

static Value n_add(VM* vm, int argc, Value* a) {
    if (a[0].kind == VAL_INT && a[1].kind == VAL_INT) {
        return int_val((int64_t)((uint64_t)a[0].as.i + (uint64_t)a[1].as.i));
    }

    return float_val(num(a[0]) + num(a[1]));
}

static Value n_sub(VM* vm, int argc, Value* a) {
    if (a[0].kind == VAL_INT && a[1].kind == VAL_INT) {
        return int_val((int64_t)((uint64_t)a[0].as.i - (uint64_t)a[1].as.i));
    }

    return float_val(num(a[0]) - num(a[1]));
}

static Value n_mul(VM* vm, int argc, Value* a) {
    if (a[0].kind == VAL_INT && a[1].kind == VAL_INT) {
        return int_val((int64_t)((uint64_t)a[0].as.i * (uint64_t)a[1].as.i));
    }

    return float_val(num(a[0]) * num(a[1]));
}

static Value n_div(VM* vm, int argc, Value* a) {
    if (a[0].kind == VAL_INT && a[1].kind == VAL_INT) {
        if (a[1].as.i == 0) {
            rt_error(vm, 0, "integer division by zero");
        }

        if (a[0].as.i == INT64_MIN && a[1].as.i == -1) {
            return int_val(INT64_MIN);
        }

        return int_val(a[0].as.i / a[1].as.i);
    }

    return float_val(num(a[0]) / num(a[1]));
}

static Value n_mod(VM* vm, int argc, Value* a) {
    if (a[0].kind == VAL_INT && a[1].kind == VAL_INT) {
        int64_t x = a[0].as.i, y = a[1].as.i;
        if (y == 0) {
            rt_error(vm, 0, "integer modulo by zero");
        }

        if (y == -1) {
            return int_val(0);
        }

        int64_t r = x % y;
        if (r != 0 && ((r < 0) != (y < 0))) {
            r += y;
        }

        return int_val(r);
    }

    double x = num(a[0]), y = num(a[1]);
    double r = fmod(x, y);
    if (r != 0 && ((r < 0) != (y < 0))) {
        r += y;
    }

    return float_val(r);
}

static Value n_pow(VM* vm, int argc, Value* a) {
    if (a[0].kind == VAL_INT && a[1].kind == VAL_INT && a[1].as.i >= 0) {
        uint64_t base = (uint64_t)a[0].as.i, result = 1;
        int64_t e = a[1].as.i;
        while (e > 0) {
            if (e & 1) {
                result *= base;
            }

            base *= base;
            e >>= 1;
        }

        return int_val((int64_t)result);
    }

    return float_val(pow(num(a[0]), num(a[1])));
}

static Value n_neg(VM* vm, int argc, Value* a) {
    if (a[0].kind == VAL_INT) {
        return int_val((int64_t)(0 - (uint64_t)a[0].as.i));
    }

    return float_val(-a[0].as.f);
}

static Value n_concat_str(VM* vm, int argc, Value* a) {
    ObjString* x = AS_STR(a[0]);
    const ObjString* y = AS_STR(a[1]);
    char* buf = pp_malloc((size_t)x->len + (size_t)y->len + 1);
    memcpy(buf, x->chars, (size_t)x->len);
    memcpy(buf + x->len, y->chars, (size_t)y->len);
    const Value r = new_string(vm, buf, x->len + y->len);
    free(buf);

    return r;
}

static Value n_eq(VM* vm, int argc, Value* a) {
    return bool_val(values_equal(a[0], a[1]));
}

static Value n_neq(VM* vm, int argc, Value* a) {
    return bool_val(!values_equal(a[0], a[1]));
}

static int cmp_values(VM* vm, Value x, Value y) {
    if (x.kind == VAL_INT && y.kind == VAL_INT) {
        return x.as.i < y.as.i ? -1 : x.as.i > y.as.i;
    }

    if ((x.kind == VAL_INT || x.kind == VAL_FLOAT) && (y.kind == VAL_INT || y.kind == VAL_FLOAT)) {
        double p = num(x), q = num(y);
        return p < q ? -1 : p > q;
    }

    if (IS_KIND(x, OBJ_STRING) && IS_KIND(y, OBJ_STRING)) {
        const ObjString* s = AS_STR(x);
        const ObjString* t = AS_STR(y);
        const int n = s->len < t->len ? s->len : t->len;
        const int c = memcmp(s->chars, t->chars, (size_t)n);
        if (c != 0) {
            return c < 0 ? -1 : 1;
        }

        return s->len < t->len ? -1 : s->len > t->len;
    }

    rt_error(vm, 0, "cannot compare %s with %s", value_type_name(x), value_type_name(y));
}

static Value n_lt(VM* vm, int argc, Value* a) {
    return bool_val(cmp_values(vm, a[0], a[1]) < 0);
}

static Value n_gt(VM* vm, int argc, Value* a) {
    return bool_val(cmp_values(vm, a[0], a[1]) > 0);
}

static Value n_le(VM* vm, int argc, Value* a) {
    return bool_val(cmp_values(vm, a[0], a[1]) <= 0);
}

static Value n_ge(VM* vm, int argc, Value* a) {
    return bool_val(cmp_values(vm, a[0], a[1]) >= 0);
}

static Value n_not(VM* vm, int argc, Value* a) {
    return bool_val(!a[0].as.b);
}

static Value n_len(VM* vm, int argc, Value* a) {
    const Value v = a[0];
    if (IS_KIND(v, OBJ_STRING)) {
        return int_val(AS_STR(v)->len);
    }

    if (IS_KIND(v, OBJ_ARRAY)) {
        return int_val(AS_ARRAY(v)->count);
    }

    if (IS_KIND(v, OBJ_OBJECT)) {
        return int_val(AS_OBJECT(v)->fields.count);
    }

    if (IS_KIND(v, OBJ_RANGE)) {
        return int_val(range_length(AS_RANGE(v)));
    }

    if (IS_KIND(v, OBJ_TUPLE)) {
        return int_val(AS_TUPLE(v)->count);
    }

    if (IS_KIND(v, OBJ_MAP) || IS_KIND(v, OBJ_SET)) {
        return int_val(AS_MAP(v)->live);
    }

    if (IS_KIND(v, OBJ_INSTANCE) && AS_INSTANCE(v)->type->cls->length) {
        return int_val(AS_INSTANCE(v)->type->cls->length(AS_INSTANCE(v)));
    }

    rt_error(vm, 0, "'#' expects a string, array, object, range, tuple, map or set, got %s", value_type_name(v));
}

static Value make_range(VM* vm, int64_t start, int64_t end, bool inclusive) {
    int64_t step = start <= end ? 1 : -1;
    if (inclusive) {
        end += step;
    }

    ObjRange* r = range_new(vm, int_val(start), int_val(end), int_val(step));
    r->inclusive = inclusive;

    return obj_val(r);
}

static Value n_range_to(VM* vm, int argc, Value* a) {
    return make_range(vm, 0, a[0].as.i, false);
}

static Value n_range(VM* vm, int argc, Value* a) {
    return make_range(vm, a[0].as.i, a[1].as.i, false);
}

static Value n_range_to_incl(VM* vm, int argc, Value* a) {
    return make_range(vm, 0, a[0].as.i, true);
}

static Value n_range_incl(VM* vm, int argc, Value* a) {
    return make_range(vm, a[0].as.i, a[1].as.i, true);
}

/* range(start, end, step) with numbers: all ints give an int range, any float makes it a float range */
static Value n_range_step(VM* vm, int argc, Value* a) {
    const double step = num_as_double(a[2]);
    if (step == 0) {
        rt_error(vm, 0, "range step cannot be 0");
    }

    for (int i = 0; i < 3; i++) {
        if (a[i].kind == VAL_FLOAT && !isfinite(a[i].as.f)) {
            rt_error(vm, 0, "range start, end and step must be finite");
        }
    }

    return obj_val(range_new(vm, a[0], a[1], a[2]));
}

/* ------------------------------------------------------------ conversions */

static Value n_str(VM* vm, int argc, Value* a) {
    return obj_val(to_string(vm, a[0]));
}

/*
 * format("%d + %.2f = %s", 1, 2.5, "x"): C printf-style formatting, checked.
 *
 *   %[flags][width][.precision][length]conversion
 *   flags      - + space 0 #
 *   width      digits or * (taken from the next argument, an int)
 *   precision  .digits or .*
 *   length     h hh l ll z j t L are accepted and ignored (ints are 64-bit)
 *   d i        int                      u o x X   int, printed as unsigned 64-bit
 *   f F e E g G a A   float or int      c         int code point or 1-character string
 *   s          any value, printed like str(); precision cuts it to that many bytes
 *   %%         a literal %
 *
 * Each spec is parsed and checked against its argument here, then rebuilt and
 * handed to the C library with the matching C type, so the script's string is
 * never used as a C format (no %n, no type confusion).
 */
#define FMT_MAX_WIDTH 1000000

typedef struct {
    VM* vm;
    StrBuf* sb;
    ObjString* fmt;
    int argc;
    Value* args;
    int next;
} FmtState;

static PP_NORETURN void fmt_error(FmtState* st, const char* msg, ...) {
    char buf[256];
    va_list ap;
    va_start(ap, msg);
    vsnprintf(buf, sizeof buf, msg, ap);
    va_end(ap);
    sb_free(st->sb);
    rt_error(st->vm, 0, "format: %s", buf);
}

static Value fmt_arg(FmtState* st, char conv) {
    if (st->next >= st->argc) {
        fmt_error(st, "'%%%c' has no argument (%d argument(s) given)", conv, st->argc - 1);
    }

    return st->args[st->next++];
}

/* digits or * */
static int fmt_number(FmtState* st, int* i, const char* what) {
    const char* s = st->fmt->chars;
    if (s[*i] == '*') {
        (*i)++;
        const Value v = fmt_arg(st, '*');
        if (v.kind != VAL_INT) {
            fmt_error(st, "'*' %s needs an int argument but got %s", what, value_type_name(v));
        }

        if (v.as.i > FMT_MAX_WIDTH || v.as.i < -FMT_MAX_WIDTH) {
            fmt_error(st, "%s %" PRId64 " is too large", what, v.as.i);
        }

        return (int)v.as.i;
    }

    int n = 0;
    while (s[*i] >= '0' && s[*i] <= '9') {
        n = n * 10 + (s[*i] - '0');
        if (n > FMT_MAX_WIDTH) {
            fmt_error(st, "%s is too large (max %d)", what, FMT_MAX_WIDTH);
        }

        (*i)++;
    }

    return n;
}

/* UTF-8 bytes of code point cp into out (at least 5 bytes), or 0 if it is not a valid code point */
static int utf8_encode(int64_t cp, char* out) {
    if (cp < 0 || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) {
        return 0;
    }

    if (cp < 0x80) {
        out[0] = (char)cp;
        return 1;
    }

    if (cp < 0x800) {
        out[0] = (char)(0xC0 | (cp >> 6));
        out[1] = (char)(0x80 | (cp & 0x3F));
        return 2;
    }

    if (cp < 0x10000) {
        out[0] = (char)(0xE0 | (cp >> 12));
        out[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
        out[2] = (char)(0x80 | (cp & 0x3F));
        return 3;
    }

    out[0] = (char)(0xF0 | (cp >> 18));
    out[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
    out[2] = (char)(0x80 | ((cp >> 6) & 0x3F));
    out[3] = (char)(0x80 | (cp & 0x3F));

    return 4;
}

static Value n_format(VM* vm, int argc, Value* a) {
    StrBuf sb;
    sb_init(&sb);
    FmtState st = {vm, &sb, AS_STR(a[0]), argc, a, 1};
    const char* s = st.fmt->chars;
    const int len = st.fmt->len;
    for (int i = 0; i < len; i++) {
        if (s[i] != '%') {
            int j = i;
            while (j < len && s[j] != '%') {
                j++;
            }

            sb_append(&sb, s + i, (size_t)(j - i));
            i = j - 1;
            continue;
        }

        const int start = i++;
        if (i < len && s[i] == '%') {
            sb_putc(&sb, '%');
            continue;
        }

        /* the spec we hand to C: '%', flags, width, precision, then our own length + conversion */
        char spec[32];
        int n = 0;
        spec[n++] = '%';
        bool left = false;
        while (i < len && s[i] && strchr("-+ 0#", s[i])) {
            if (s[i] == '-') {
                left = true;
            }

            if (n < 8) {
                spec[n++] = s[i];
            }

            i++;
        }

        int width = fmt_number(&st, &i, "width");
        if (width < 0) { /* a negative * width means left-aligned, like C */
            left = true;
            width = -width;
            spec[n++] = '-';
        }

        int prec = -1;
        if (i < len && s[i] == '.') {
            i++;
            prec = fmt_number(&st, &i, "precision");
            if (prec < 0) {
                prec = -1; /* a negative * precision means none, like C */
            }
        }
        while (i < len && s[i] && strchr("hlzjtL", s[i])) {
            i++;
        }

        if (i >= len) {
            fmt_error(&st, "the spec at position %d has no conversion character", start);
        }

        const char conv = s[i];

        if (width) {
            n += snprintf(spec + n, sizeof spec - (size_t)n, "%d", width);
        }

        if (prec >= 0) {
            n += snprintf(spec + n, sizeof spec - (size_t)n, ".%d", prec);
        }

        switch (conv) {
            case 'd':
            case 'i': {
                const Value v = fmt_arg(&st, conv);
                if (v.kind != VAL_INT) {
                    fmt_error(&st, "'%%%c' needs an int but got %s", conv, value_type_name(v));
                }

                snprintf(spec + n, sizeof spec - (size_t)n, "%s", PRId64);
                sb_printf(&sb, spec, v.as.i);
                break;
            }
            case 'u':
            case 'o':
            case 'x':
            case 'X': {
                const Value v = fmt_arg(&st, conv);
                if (v.kind != VAL_INT) {
                    fmt_error(&st, "'%%%c' needs an int but got %s", conv, value_type_name(v));
                }

                const char* m = conv == 'u' ? PRIu64 : conv == 'o' ? PRIo64 : conv == 'x' ? PRIx64 : PRIX64;
                snprintf(spec + n, sizeof spec - (size_t)n, "%s", m);
                sb_printf(&sb, spec, (uint64_t)v.as.i);
                break;
            }
            case 'f':
            case 'F':
            case 'e':
            case 'E':
            case 'g':
            case 'G':
            case 'a':
            case 'A': {
                const Value v = fmt_arg(&st, conv);
                double d;
                if (v.kind == VAL_FLOAT) {
                    d = v.as.f;
                } else if (v.kind == VAL_INT) {
                    d = (double)v.as.i;
                } else {
                    fmt_error(&st, "'%%%c' needs a float or int but got %s", conv, value_type_name(v));
                }

                spec[n++] = conv;
                spec[n] = '\0';
                sb_printf(&sb, spec, d);
                break;
            }
            case 'c': {
                const Value v = fmt_arg(&st, conv);
                char buf[5];
                int blen;
                if (v.kind == VAL_INT) {
                    blen = utf8_encode(v.as.i, buf);
                    if (!blen) {
                        fmt_error(&st, "'%%c' got %" PRId64 ", which is not a Unicode code point", v.as.i);
                    }
                } else if (IS_KIND(v, OBJ_STRING) && AS_STR(v)->len >= 1 && AS_STR(v)->len <= 4) {
                    blen = AS_STR(v)->len;
                    memcpy(buf, AS_STR(v)->chars, (size_t)blen);
                } else {
                    fmt_error(&st, "'%%c' needs an int code point or a 1-character string but got %s", value_type_name(v));
                }

                if (prec >= 0) {
                    fmt_error(&st, "'%%c' does not take a precision");
                }

                /* pad by characters, not bytes */
                if (!left) {
                    for (int k = 1; k < width; k++) {
                        sb_putc(&sb, ' ');
                    }
                }

                sb_append(&sb, buf, (size_t)blen);
                if (left) {
                    for (int k = 1; k < width; k++) {
                        sb_putc(&sb, ' ');
                    }
                }

                break;
            }
            case 's': {
                const Value v = fmt_arg(&st, conv);
                StrBuf tmp;
                sb_init(&tmp);
                if (IS_KIND(v, OBJ_STRING)) {
                    sb_append(&tmp, AS_STR(v)->chars, (size_t)AS_STR(v)->len);
                } else {
                    value_format(&tmp, v, true, 0);
                }

                spec[n++] = 's';
                spec[n] = '\0';
                sb_printf(&sb, spec, tmp.data);
                sb_free(&tmp);
                break;
            }
            case 'n':
            case 'p': fmt_error(&st, "'%%%c' is not supported", conv);
            default:
                fmt_error(&st, "unknown conversion '%%%c' (use d i u o x X f F e E g G a A c s or %%%%)", conv);
        }
    }

    if (st.next < argc) {
        const int extra = argc - st.next;
        fmt_error(&st, "%d argument(s) left over: the format uses %d of %d", extra, st.next - 1, argc - 1);
    }

    const Value r = new_string(vm, sb.data ? sb.data : "", (int)sb.len);
    sb_free(&sb);

    return r;
}

static Value n_type_of(VM* vm, int argc, Value* a) {
    return obj_val(str_cnew(vm, value_type_name(a[0])));
}

static bool parse_int_str(ObjString* s, int64_t* out) {
    const char* p = s->chars;
    while (*p == ' ' || *p == '\t') {
        p++;
    }

    if (!*p) {
        return false;
    }

    char* end;
    errno = 0;
    const long long v = strtoll(p, &end, 10);
    while (*end == ' ' || *end == '\t' || *end == '\r' || *end == '\n') {
        end++;
    }

    if (*end || errno) {
        return false;
    }

    *out = v;
    return true;
}

static bool parse_float_str(ObjString* s, double* out) {
    const char* p = s->chars;
    while (*p == ' ' || *p == '\t') {
        p++;
    }

    if (!*p) {
        return false;
    }

    char* end;
    const double v = strtod(p, &end);
    while (*end == ' ' || *end == '\t' || *end == '\r' || *end == '\n') {
        end++;
    }

    if (*end) {
        return false;
    }

    *out = v;
    return true;
}

static Value n_int(VM* vm, int argc, Value* a) {
    const Value v = a[0];
    switch (v.kind) {
        case VAL_INT: return v;
        case VAL_FLOAT:
            if (isnan(v.as.f) || isinf(v.as.f)) {
                rt_error(vm, 0, "cannot convert %f to int", v.as.f);
            }

            return int_val((int64_t)v.as.f);
        case VAL_BOOL: return int_val(v.as.b ? 1 : 0);
        default: break;
    }

    if (IS_KIND(v, OBJ_STRING)) {
        int64_t r;
        if (!parse_int_str(AS_STR(v), &r)) {
            rt_error(vm, 0, "cannot convert \"%s\" to int", AS_STR(v)->chars);
        }

        return int_val(r);
    }

    if (IS_KIND(v, OBJ_ENUM_VALUE)) {
        const ObjEnumValue* ev = AS_ENUMV(v);
        const Value inner = ev->en->variants[ev->variant].value;
        if (inner.kind == VAL_INT) {
            return inner;
        }
    }

    rt_error(vm, 0, "cannot convert a %s to int", value_type_name(v));
}

static Value n_float(VM* vm, int argc, Value* a) {
    const Value v = a[0];
    if (v.kind == VAL_FLOAT) {
        return v;
    }

    if (v.kind == VAL_INT) {
        return float_val((double)v.as.i);
    }

    if (IS_KIND(v, OBJ_STRING)) {
        double r;
        if (!parse_float_str(AS_STR(v), &r)) {
            rt_error(vm, 0, "cannot convert \"%s\" to float", AS_STR(v)->chars);
        }

        return float_val(r);
    }

    rt_error(vm, 0, "cannot convert a %s to float", value_type_name(v));
}

static Value n_parse_int(VM* vm, int argc, Value* a) {
    int64_t r;
    if (!parse_int_str(AS_STR(a[0]), &r)) {
        return make_none(vm);
    }

    return make_some(vm, int_val(r));
}

static Value n_parse_float(VM* vm, int argc, Value* a) {
    double r;
    if (!parse_float_str(AS_STR(a[0]), &r)) {
        return make_none(vm);
    }

    return make_some(vm, float_val(r));
}

static Value n_enum_value(VM* vm, int argc, Value* a) {
    if (!IS_KIND(a[0], OBJ_ENUM_VALUE)) {
        rt_error(vm, 0, "enum_value expects an enum value, got %s", value_type_name(a[0]));
    }

    const ObjEnumValue* ev = AS_ENUMV(a[0]);

    return ev->en->variants[ev->variant].value;
}

/* ----------------------------------------------------------------- arrays */

static Value n_push(VM* vm, int argc, Value* a) {
    if (a[1].kind == VAL_VOID) {
        rt_error(vm, 0, "cannot push a void value");
    }

    array_push(AS_ARRAY(a[0]), a[1]);

    return void_val();
}

static Value n_pop(VM* vm, int argc, Value* a) {
    ObjArray* arr = AS_ARRAY(a[0]);
    if (arr->count == 0) {
        return make_none(vm);
    }

    return make_some(vm, arr->items[--arr->count]);
}

static Value n_insert(VM* vm, int argc, Value* a) {
    ObjArray* arr = AS_ARRAY(a[0]);
    int64_t i = a[1].as.i;
    if (i < 0) {
        i += arr->count + 1;
    }

    if (i < 0 || i > arr->count) {
        rt_error(vm, 0, "insert index %" PRId64 " out of range (length %d)", a[1].as.i, arr->count);
    }

    GROW(arr->items, arr->count, arr->cap);
    memmove(&arr->items[i + 1], &arr->items[i], sizeof(Value) * (size_t)(arr->count - i));
    arr->items[i] = a[2];
    arr->count++;

    return void_val();
}

static Value n_remove(VM* vm, int argc, Value* a) {
    ObjArray* arr = AS_ARRAY(a[0]);
    int64_t i = a[1].as.i;
    if (i < 0) {
        i += arr->count;
    }

    if (i < 0 || i >= arr->count) {
        rt_error(vm, 0, "remove index %" PRId64 " out of range (length %d)", a[1].as.i, arr->count);
    }

    Value v = arr->items[i];
    memmove(&arr->items[i], &arr->items[i + 1], sizeof(Value) * (size_t)(arr->count - i - 1));
    arr->count--;

    return v;
}

static Value n_concat(VM* vm, int argc, Value* a) {
    ObjArray* x = AS_ARRAY(a[0]);
    ObjArray* y = AS_ARRAY(a[1]);
    ObjArray* r = array_new(vm, x->count + y->count);
    for (int i = 0; i < x->count; i++) {
        r->items[r->count++] = x->items[i];
    }

    for (int i = 0; i < y->count; i++) {
        r->items[r->count++] = y->items[i];
    }

    return obj_val(r);
}

static Value n_multiply(VM* vm, int argc, Value* a) {
    ObjArray* x = AS_ARRAY(a[0]);
    int64_t n = a[1].as.i;
    if (n < 0) {
        rt_error(vm, 0, "cannot repeat an array a negative number of times");
    }

    if (n > 0 && (int64_t)x->count * n > 100000000) {
        rt_error(vm, 0, "repeated array would be too large");
    }

    ObjArray* r = array_new(vm, (int)(x->count * n));
    for (int64_t k = 0; k < n; k++) {
        for (int i = 0; i < x->count; i++) {
            r->items[r->count++] = x->items[i];
        }
    }

    return obj_val(r);
}

static Value n_repeat_str(VM* vm, int argc, Value* a) {
    const ObjString* s = AS_STR(a[0]);
    const int64_t n = a[1].as.i;
    if (n < 0) {
        rt_error(vm, 0, "cannot repeat a string a negative number of times");
    }

    if ((int64_t)s->len * n > 500000000) {
        rt_error(vm, 0, "repeated string would be too large");
    }

    StrBuf sb;
    sb_init(&sb);
    for (int64_t i = 0; i < n; i++) {
        sb_append(&sb, s->chars, (size_t)s->len);
    }

    const Value r = new_string(vm, sb.data, (int)sb.len);
    sb_free(&sb);

    return r;
}

static void clamp_slice(int64_t len, int64_t* start, int64_t* end) {
    if (*start < 0) {
        *start += len;
    }

    if (*end < 0) {
        *end += len;
    }

    if (*start < 0) {
        *start = 0;
    }

    if (*end > len) {
        *end = len;
    }

    if (*start > *end) {
        *start = *end;
    }
}

static Value n_slice(VM* vm, int argc, Value* a) {
    int64_t start = a[1].as.i;
    if (IS_KIND(a[0], OBJ_STRING)) {
        ObjString* s = AS_STR(a[0]);
        int64_t end = argc > 2 ? a[2].as.i : s->len;
        clamp_slice(s->len, &start, &end);
        return new_string(vm, s->chars + start, (int)(end - start));
    }

    ObjArray* arr = AS_ARRAY(a[0]);
    int64_t end = argc > 2 ? a[2].as.i : arr->count;
    clamp_slice(arr->count, &start, &end);
    ObjArray* r = array_new(vm, (int)(end - start));
    for (int64_t i = start; i < end; i++) {
        r->items[r->count++] = arr->items[i];
    }

    return obj_val(r);
}

static Value n_contains(VM* vm, int argc, Value* a) {
    const ObjArray* arr = AS_ARRAY(a[0]);
    for (int i = 0; i < arr->count; i++) {
        if (values_equal(arr->items[i], a[1])) {
            return bool_val(true);
        }
    }

    return bool_val(false);
}

static const char* find_sub(ObjString* s, ObjString* sub, int from) {
    if (sub->len == 0) {
        return s->chars + from;
    }

    for (int i = from; i + sub->len <= s->len; i++) {
        if (memcmp(s->chars + i, sub->chars, (size_t)sub->len) == 0) {
            return s->chars + i;
        }
    }

    return NULL;
}

static Value n_contains_str(VM* vm, int argc, Value* a) {
    return bool_val(find_sub(AS_STR(a[0]), AS_STR(a[1]), 0) != NULL);
}

static Value n_index_of(VM* vm, int argc, Value* a) {
    const ObjArray* arr = AS_ARRAY(a[0]);
    for (int i = 0; i < arr->count; i++) {
        if (values_equal(arr->items[i], a[1])) {
            return make_some(vm, int_val(i));
        }
    }

    return make_none(vm);
}

static Value n_index_of_str(VM* vm, int argc, Value* a) {
    ObjString* s = AS_STR(a[0]);
    const char* p = find_sub(s, AS_STR(a[1]), 0);
    if (!p) {
        return make_none(vm);
    }

    return make_some(vm, int_val(p - s->chars));
}

static Value n_reverse(VM* vm, int argc, Value* a) {
    ObjArray* arr = AS_ARRAY(a[0]);
    ObjArray* r = array_new(vm, arr->count);
    for (int i = arr->count - 1; i >= 0; i--) {
        r->items[r->count++] = arr->items[i];
    }

    return obj_val(r);
}

static Value n_reverse_str(VM* vm, int argc, Value* a) {
    ObjString* s = AS_STR(a[0]);
    char* buf = pp_malloc((size_t)s->len + 1);
    for (int i = 0; i < s->len; i++) {
        buf[i] = s->chars[s->len - 1 - i];
    }

    const Value r = new_string(vm, buf, s->len);
    free(buf);

    return r;
}

static Value n_copy(VM* vm, int argc, Value* a) {
    Value v = a[0];
    if (IS_KIND(v, OBJ_ARRAY)) {
        ObjArray* x = AS_ARRAY(v);
        ObjArray* r = array_new(vm, x->count);
        for (int i = 0; i < x->count; i++) {
            r->items[r->count++] = x->items[i];
        }

        return obj_val(r);
    }

    if (IS_KIND(v, OBJ_OBJECT)) {
        ObjObject* x = AS_OBJECT(v);
        ObjObject* r = object_new(vm);
        for (int i = 0; i < x->fields.count; i++) {
            table_set(&r->fields, x->fields.entries[i].key, x->fields.entries[i].value);
        }

        r->tag = x->tag;
        return obj_val(r);
    }

    if (IS_KIND(v, OBJ_MAP) || IS_KIND(v, OBJ_SET)) {
        ObjMap* x = AS_MAP(v);
        ObjMap* r = map_new(vm, v.as.o->kind);
        for (int i = 0; i < x->count; i++) {
            if (x->entries[i].live) {
                map_put(r, x->entries[i].key, x->entries[i].hash, x->entries[i].value);
            }
        }

        return obj_val(r);
    }

    if (IS_KIND(v, OBJ_INSTANCE) && AS_INSTANCE(v)->type->cls->copy) {
        return obj_val(AS_INSTANCE(v)->type->cls->copy(vm, AS_INSTANCE(v)));
    }

    return v; /* tuples cannot change, so they need no copy */
}

/* tuple(1, "a"): a fixed-size, immutable group of values, typed tuple<int, string> */
static Value n_tuple(VM* vm, int argc, Value* a) {
    ObjTuple* t = tuple_new(vm, argc);
    for (int i = 0; i < argc; i++) {
        t->items[i] = a[i];
    }

    return obj_val(t);
}

/* --- sorting (merge sort; GC is paused because a comparator may run code) */

static Value sort_cmp_fn;
static bool sort_has_cmp;

static int sort_compare(VM* vm, Value x, Value y) {
    if (!sort_has_cmp) {
        return cmp_values(vm, x, y);
    }

    Value args[2] = {x, y};
    const Value r = vm_call(vm, sort_cmp_fn, 2, args);
    if (r.kind == VAL_INT) {
        return r.as.i < 0 ? -1 : r.as.i > 0;
    }

    if (r.kind == VAL_FLOAT) {
        return r.as.f < 0 ? -1 : r.as.f > 0;
    }

    if (r.kind == VAL_BOOL) {
        return r.as.b ? -1 : 1;
    }

    rt_error(vm, 0, "sort comparator must return a number or bool, got %s", value_type_name(r));
}

static void merge_sort(VM* vm, Value* items, Value* tmp, int n) {
    if (n < 2) {
        return;
    }

    const int mid = n / 2;
    merge_sort(vm, items, tmp, mid);
    merge_sort(vm, items + mid, tmp, n - mid);
    int i = 0, j = mid, k = 0;
    while (i < mid && j < n) {
        tmp[k++] = sort_compare(vm, items[j], items[i]) < 0 ? items[j++] : items[i++];
    }
    while (i < mid) {
        tmp[k++] = items[i++];
    }
    while (j < n) {
        tmp[k++] = items[j++];
    }

    memcpy(items, tmp, sizeof(Value) * (size_t)n);
}

static Value do_sort(VM* vm, Value arrv, bool has_cmp, Value cmp) {
    ObjArray* src = AS_ARRAY(arrv);
    ObjArray* r = array_new(vm, src->count);
    for (int i = 0; i < src->count; i++) {
        r->items[r->count++] = src->items[i];
    }

    Value* tmp = pp_malloc(sizeof(Value) * (size_t)(r->count ? r->count : 1));
    Value saved_fn = sort_cmp_fn;
    bool saved_has = sort_has_cmp;
    sort_cmp_fn = cmp;
    sort_has_cmp = has_cmp;
    vm->gc_disabled++;
    merge_sort(vm, r->items, tmp, r->count);
    vm->gc_disabled--;
    sort_cmp_fn = saved_fn;
    sort_has_cmp = saved_has;
    free(tmp);

    return obj_val(r);
}

static Value n_sort(VM* vm, int argc, Value* a) {
    return do_sort(vm, a[0], false, void_val());
}

static Value n_sort_by(VM* vm, int argc, Value* a) {
    return do_sort(vm, a[0], true, a[1]);
}

static Value n_join(VM* vm, int argc, Value* a) {
    const ObjArray* arr = AS_ARRAY(a[0]);
    const ObjString* sep = AS_STR(a[1]);
    StrBuf sb;
    sb_init(&sb);
    for (int i = 0; i < arr->count; i++) {
        if (i) {
            sb_append(&sb, sep->chars, (size_t)sep->len);
        }

        value_format(&sb, arr->items[i], true, 0);
    }

    const Value r = new_string(vm, sb.data, (int)sb.len);
    sb_free(&sb);

    return r;
}

static Value n_map(VM* vm, int argc, Value* a) {
    const ObjArray* src = AS_ARRAY(a[0]);
    ObjArray* out = array_new(vm, src->count);
    const int base = vm->sp;
    vm_push(vm, obj_val(out));
    for (int i = 0; i < src->count; i++) {
        vm_push(vm, src->items[i]);
        const Value r = vm_call(vm, a[1], 1, &vm->stack[vm->sp - 1]);
        vm->sp--;
        if (r.kind == VAL_VOID) {
            rt_error(vm, 0, "map: the function returned no value");
        }

        array_push(out, r);
    }

    vm->sp = base;

    return obj_val(out);
}

static Value n_filter(VM* vm, int argc, Value* a) {
    const ObjArray* src = AS_ARRAY(a[0]);
    ObjArray* out = array_new(vm, 0);
    const int base = vm->sp;
    vm_push(vm, obj_val(out));
    for (int i = 0; i < src->count; i++) {
        const Value item = src->items[i];
        vm_push(vm, item);
        const Value r = vm_call(vm, a[1], 1, &vm->stack[vm->sp - 1]);
        vm->sp--;
        if (r.kind != VAL_BOOL) {
            rt_error(vm, 0, "filter: the function must return a bool, got %s", value_type_name(r));
        }

        if (r.as.b) {
            array_push(out, item);
        }
    }

    vm->sp = base;

    return obj_val(out);
}

static Value n_reduce(VM* vm, int argc, Value* a) {
    const ObjArray* src = AS_ARRAY(a[0]);
    const int base = vm->sp;
    vm_push(vm, a[2]); /* accumulator */
    for (int i = 0; i < src->count; i++) {
        vm_push(vm, src->items[i]);
        const Value r = vm_call(vm, a[1], 2, &vm->stack[base]);
        vm->sp = base + 1;
        vm->stack[base] = r;
    }

    const Value r = vm->stack[base];
    vm->sp = base;

    return r;
}

/* ---------------------------------------------------------------- objects */

static Value n_keys(VM* vm, int argc, Value* a) {
    ObjObject* o = AS_OBJECT(a[0]);
    ObjArray* r = array_new(vm, o->fields.count);
    for (int i = 0; i < o->fields.count; i++) {
        r->items[r->count++] = obj_val(o->fields.entries[i].key);
    }

    return obj_val(r);
}

static Value n_values(VM* vm, int argc, Value* a) {
    ObjObject* o = AS_OBJECT(a[0]);
    ObjArray* r = array_new(vm, o->fields.count);
    for (int i = 0; i < o->fields.count; i++) {
        r->items[r->count++] = o->fields.entries[i].value;
    }

    return obj_val(r);
}

static Value n_has(VM* vm, int argc, Value* a) {
    return bool_val(table_find(&AS_OBJECT(a[0])->fields, AS_STR(a[1])) != NULL);
}

static Value n_remove_key(VM* vm, int argc, Value* a) {
    ObjObject* o = AS_OBJECT(a[0]);
    if (o->tag) {
        rt_error(vm, 0, "cannot remove fields from an object of type %s", o->tag->name->chars);
    }

    return bool_val(table_remove(&o->fields, AS_STR(a[1])));
}

/* ---------------------------------------------------------------- strings */

static Value n_split(VM* vm, int argc, Value* a) {
    ObjString* s = AS_STR(a[0]);
    ObjString* sep = AS_STR(a[1]);
    ObjArray* r = array_new(vm, 0);
    if (sep->len == 0) {
        for (int i = 0; i < s->len; i++) {
            array_push(r, new_string(vm, s->chars + i, 1));
        }

        return obj_val(r);
    }

    int start = 0;
    for (;;) {
        const char* p = find_sub(s, sep, start);
        if (!p) {
            break;
        }

        int at = (int)(p - s->chars);
        array_push(r, new_string(vm, s->chars + start, at - start));
        start = at + sep->len;
    }

    array_push(r, new_string(vm, s->chars + start, s->len - start));

    return obj_val(r);
}

static Value n_upper(VM* vm, int argc, Value* a) {
    const ObjString* s = AS_STR(a[0]);
    ObjString* r = str_new(vm, s->chars, s->len);
    for (int i = 0; i < r->len; i++) {
        if (r->chars[i] >= 'a' && r->chars[i] <= 'z') {
            r->chars[i] = (char)(r->chars[i] - 32);
        }
    }

    r->hash = hash_chars(r->chars, r->len);

    return obj_val(r);
}

static Value n_lower(VM* vm, int argc, Value* a) {
    const ObjString* s = AS_STR(a[0]);
    ObjString* r = str_new(vm, s->chars, s->len);
    for (int i = 0; i < r->len; i++) {
        if (r->chars[i] >= 'A' && r->chars[i] <= 'Z') {
            r->chars[i] = (char)(r->chars[i] + 32);
        }
    }

    r->hash = hash_chars(r->chars, r->len);

    return obj_val(r);
}

static Value n_trim(VM* vm, int argc, Value* a) {
    const ObjString* s = AS_STR(a[0]);
    int start = 0, end = s->len;
    while (start < end && (s->chars[start] == ' ' || s->chars[start] == '\t' || s->chars[start] == '\n' || s->chars[start] == '\r')) {
        start++;
    }
    while (end > start && (s->chars[end - 1] == ' ' || s->chars[end - 1] == '\t' || s->chars[end - 1] == '\n' || s->chars[end - 1] == '\r')) {
        end--;
    }

    return new_string(vm, s->chars + start, end - start);
}

static Value n_replace(VM* vm, int argc, Value* a) {
    const ObjString* s = AS_STR(a[0]);
    ObjString* from = AS_STR(a[1]);
    const ObjString* to = AS_STR(a[2]);
    if (from->len == 0) {
        return a[0];
    }

    StrBuf sb;
    sb_init(&sb);
    int i = 0;
    while (i < s->len) {
        if (i + from->len <= s->len && memcmp(s->chars + i, from->chars, (size_t)from->len) == 0) {
            sb_append(&sb, to->chars, (size_t)to->len);
            i += from->len;
        } else {
            sb_putc(&sb, s->chars[i++]);
        }
    }

    const Value r = new_string(vm, sb.data, (int)sb.len);
    sb_free(&sb);

    return r;
}

static Value n_starts_with(VM* vm, int argc, Value* a) {
    const ObjString* s = AS_STR(a[0]);
    const ObjString* p = AS_STR(a[1]);

    return bool_val(p->len <= s->len && memcmp(s->chars, p->chars, (size_t)p->len) == 0);
}

static Value n_ends_with(VM* vm, int argc, Value* a) {
    const ObjString* s = AS_STR(a[0]);
    const ObjString* p = AS_STR(a[1]);

    return bool_val(p->len <= s->len && memcmp(s->chars + s->len - p->len, p->chars, (size_t)p->len) == 0);
}

static Value n_chars(VM* vm, int argc, Value* a) {
    ObjString* s = AS_STR(a[0]);
    ObjArray* r = array_new(vm, s->len);
    for (int i = 0; i < s->len;) {
        const unsigned char c = (unsigned char)s->chars[i];
        int len = c < 0x80 ? 1 : (c >> 5) == 0x6 ? 2 : (c >> 4) == 0xE ? 3 : (c >> 3) == 0x1E ? 4 : 1;
        if (i + len > s->len) {
            len = s->len - i;
        }

        array_push(r, new_string(vm, s->chars + i, len));
        i += len;
    }

    return obj_val(r);
}

static Value n_ord(VM* vm, int argc, Value* a) {
    const ObjString* s = AS_STR(a[0]);
    if (s->len == 0) {
        rt_error(vm, 0, "ord expects a non-empty string");
    }

    const unsigned char* p = (const unsigned char*)s->chars;
    uint32_t cp;
    if (p[0] < 0x80) {
        cp = p[0];
    } else if ((p[0] >> 5) == 0x6 && s->len >= 2) {
        cp = ((uint32_t)(p[0] & 0x1F) << 6) | (p[1] & 0x3F);
    } else if ((p[0] >> 4) == 0xE && s->len >= 3) {
        cp = ((uint32_t)(p[0] & 0x0F) << 12) | ((uint32_t)(p[1] & 0x3F) << 6) | (p[2] & 0x3F);
    } else if (s->len >= 4) {
        cp = ((uint32_t)(p[0] & 0x07) << 18) | ((uint32_t)(p[1] & 0x3F) << 12) | ((uint32_t)(p[2] & 0x3F) << 6) | (p[3] & 0x3F);
    } else {
        cp = p[0];
    }

    return int_val(cp);
}

static Value n_chr(VM* vm, int argc, Value* a) {
    const int64_t cp = a[0].as.i;
    if (cp < 0 || cp > 0x10FFFF) {
        rt_error(vm, 0, "chr: %" PRId64 " is not a valid code point", cp);
    }

    char buf[4];
    int n;
    if (cp < 0x80) {
        buf[0] = (char)cp;
        n = 1;
    } else if (cp < 0x800) {
        buf[0] = (char)(0xC0 | (cp >> 6));
        buf[1] = (char)(0x80 | (cp & 0x3F));
        n = 2;
    } else if (cp < 0x10000) {
        buf[0] = (char)(0xE0 | (cp >> 12));
        buf[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
        buf[2] = (char)(0x80 | (cp & 0x3F));
        n = 3;
    } else {
        buf[0] = (char)(0xF0 | (cp >> 18));
        buf[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
        buf[2] = (char)(0x80 | ((cp >> 6) & 0x3F));
        buf[3] = (char)(0x80 | (cp & 0x3F));
        n = 4;
    }

    return new_string(vm, buf, n);
}

/* ------------------------------------------------------------------- math */

static Value n_abs(VM* vm, int argc, Value* a) {
    if (a[0].kind == VAL_INT) {
        return int_val(a[0].as.i < 0 ? -a[0].as.i : a[0].as.i);
    }

    return float_val(fabs(a[0].as.f));
}

static Value n_min(VM* vm, int argc, Value* a) {
    return cmp_values(vm, a[1], a[0]) < 0 ? a[1] : a[0];
}

static Value n_max(VM* vm, int argc, Value* a) {
    return cmp_values(vm, a[1], a[0]) > 0 ? a[1] : a[0];
}

static Value float_to_int(VM* vm, double d, const char* what) {
    if (isnan(d) || isinf(d) || d > 9.2e18 || d < -9.2e18) {
        rt_error(vm, 0, "%s: result does not fit in an int", what);
    }

    return int_val((int64_t)d);
}

static Value n_floor(VM* vm, int argc, Value* a) {
    return a[0].kind == VAL_INT ? a[0] : float_to_int(vm, floor(a[0].as.f), "floor");
}

static Value n_ceil(VM* vm, int argc, Value* a) {
    return a[0].kind == VAL_INT ? a[0] : float_to_int(vm, ceil(a[0].as.f), "ceil");
}

static Value n_round(VM* vm, int argc, Value* a) {
    return a[0].kind == VAL_INT ? a[0] : float_to_int(vm, round(a[0].as.f), "round");
}

static Value n_sqrt(VM* vm, int argc, Value* a) {
    return float_val(sqrt(num(a[0])));
}

static Value n_powf(VM* vm, int argc, Value* a) {
    return float_val(pow(num(a[0]), num(a[1])));
}

/* ------------------------------------------------------------------- misc */

static Value n_panic(VM* vm, int argc, Value* a) {
    rt_error(vm, 0, "panic: %s", to_string(vm, a[0])->chars);
}

/* call(f, args...) == f(args...), so callbacks can also be called as cb.call(x) */
static Value n_call(VM* vm, int argc, Value* a) {
    return vm_call(vm, a[0], argc - 1, a + 1);
}

static Value n_assert(VM* vm, int argc, Value* a) {
    if (!a[0].as.b) {
        if (argc > 1) {
            rt_error(vm, 0, "assertion failed: %s", to_string(vm, a[1])->chars);
        }

        rt_error(vm, 0, "assertion failed");
    }

    return void_val();
}

static Value n_clock(VM* vm, int argc, Value* a) {
#ifdef _WIN32
    static LARGE_INTEGER freq;
    LARGE_INTEGER now;
    if (freq.QuadPart == 0) {
        QueryPerformanceFrequency(&freq);
    }

    QueryPerformanceCounter(&now);
    return float_val((double)now.QuadPart / (double)freq.QuadPart);
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);

    return float_val((double)ts.tv_sec + (double)ts.tv_nsec / 1e9);
#endif
}

static Value n_defined(VM* vm, int argc, Value* a) {
    return bool_val(env_find(vm_current_env(vm), AS_STR(a[0]), NULL) != NULL);
}

/* ------------------------------------------------------------ registration */

void builtins_register(VM* vm) {
    /* operators */
    def(vm, "+", "(number a, number b)", n_add);
    def(vm, "+", "(string a, string b)", n_concat_str);
    def(vm, "-", "(number a, number b)", n_sub);
    def(vm, "-", "(number a)", n_neg);
    def(vm, "*", "(number a, number b)", n_mul);
    def(vm, "*", "(string s, int n)", n_repeat_str);
    def(vm, "/", "(number a, number b)", n_div);
    def(vm, "%", "(number a, number b)", n_mod);
    def(vm, "**", "(number a, number b)", n_pow);
    def(vm, "==", "(any a, any b) -> bool", n_eq);
    def(vm, "!=", "(any a, any b) -> bool", n_neq);
    def(vm, "<", "(any a, any b) -> bool", n_lt);
    def(vm, ">", "(any a, any b) -> bool", n_gt);
    def(vm, "<=", "(any a, any b) -> bool", n_le);
    def(vm, ">=", "(any a, any b) -> bool", n_ge);
    def(vm, "!", "(bool a) -> bool", n_not);
    def(vm, "#", "(any collection) -> int", n_len);
    def(vm, "...", "(int end) -> range", n_range_to);
    def(vm, "...", "(int start, int end) -> range", n_range);
    def(vm, "...=", "(int end) -> range", n_range_to_incl);
    def(vm, "...=", "(int start, int end) -> range", n_range_incl);
    def(vm, "range", "(number start, number end, number step) -> range", n_range_step);

    /* printing & input live in the `io` module (src/modules/io.c) */

    /* conversions & reflection */
    def(vm, "str", "(any value) -> string", n_str);
    def(vm, "call", "(function f, ...)", n_call);
    def(vm, "int", "(any value) -> int", n_int);
    def(vm, "float", "(any value) -> float", n_float);
    def(vm, "parse_int", "(string s)", n_parse_int);
    def(vm, "parse_float", "(string s)", n_parse_float);
    def(vm, "type_of", "(any value) -> string", n_type_of);
    def(vm, "enum_value", "(any variant)", n_enum_value);

    /* arrays */
    def(vm, "push", "(any[] array, any item)", n_push);
    def(vm, "pop", "(any[] array)", n_pop);
    def(vm, "insert", "(any[] array, int index, any item)", n_insert);
    def(vm, "remove", "(any[] array, int index)", n_remove);
    def(vm, "concat", "(any[] a, any[] b)", n_concat);
    def(vm, "concat", "(string a, string b) -> string", n_concat_str);
    def(vm, "multiply", "(any[] array, int count)", n_multiply);
    def(vm, "repeat", "(any[] array, int count)", n_multiply);
    def(vm, "repeat", "(string s, int count) -> string", n_repeat_str);
    def(vm, "slice", "(any[] array, int start)", n_slice);
    def(vm, "slice", "(any[] array, int start, int end)", n_slice);
    def(vm, "slice", "(string s, int start) -> string", n_slice);
    def(vm, "slice", "(string s, int start, int end) -> string", n_slice);
    def(vm, "contains", "(any[] array, any item) -> bool", n_contains);
    def(vm, "contains", "(string s, string sub) -> bool", n_contains_str);
    def(vm, "index_of", "(any[] array, any item)", n_index_of);
    def(vm, "index_of", "(string s, string sub)", n_index_of_str);
    def(vm, "reverse", "(any[] array)", n_reverse);
    def(vm, "reverse", "(string s) -> string", n_reverse_str);
    def(vm, "sort", "(any[] array)", n_sort);
    def(vm, "sort", "(any[] array, function compare)", n_sort_by);
    def(vm, "join", "(any[] array, string separator) -> string", n_join);
    def(vm, "map", "(any[] array, function f)", n_map);
    def(vm, "filter", "(any[] array, function predicate)", n_filter);
    def(vm, "reduce", "(any[] array, function f, any initial)", n_reduce);
    def(vm, "copy", "(any value)", n_copy);
    def(vm, "tuple", "(any first, ...)", n_tuple);
    {
        const Entry* e = table_find(&vm->builtins->vars, str_cintern(vm, "tuple"));
        const ObjOverload* set = (ObjOverload*)e->value.as.o;
        vm->tuple_ctor = set->items[0];
    }

    /* objects */
    def(vm, "keys", "(object o) -> string[]", n_keys);
    def(vm, "values", "(object o)", n_values);
    def(vm, "has", "(object o, string key) -> bool", n_has);
    def(vm, "remove_key", "(object o, string key) -> bool", n_remove_key);

    /* strings */
    def(vm, "format", "(string format, ...) -> string", n_format);
    def(vm, "split", "(string s, string separator) -> string[]", n_split);
    def(vm, "upper", "(string s) -> string", n_upper);
    def(vm, "lower", "(string s) -> string", n_lower);
    def(vm, "trim", "(string s) -> string", n_trim);
    def(vm, "replace", "(string s, string from, string to) -> string", n_replace);
    def(vm, "starts_with", "(string s, string prefix) -> bool", n_starts_with);
    def(vm, "ends_with", "(string s, string suffix) -> bool", n_ends_with);
    def(vm, "chars", "(string s) -> string[]", n_chars);
    def(vm, "ord", "(string s) -> int", n_ord);
    def(vm, "chr", "(int code) -> string", n_chr);

    /* math */
    def(vm, "abs", "(number x)", n_abs);
    def(vm, "min", "(any a, any b)", n_min);
    def(vm, "max", "(any a, any b)", n_max);
    def(vm, "floor", "(number x) -> int", n_floor);
    def(vm, "ceil", "(number x) -> int", n_ceil);
    def(vm, "round", "(number x) -> int", n_round);
    def(vm, "sqrt", "(number x) -> float", n_sqrt);
    def(vm, "pow", "(number base, number exponent) -> float", n_powf);

    /* misc */
    def(vm, "panic", "(any message)", n_panic);
    def(vm, "assert", "(bool condition)", n_assert);
    def(vm, "assert", "(bool condition, any message)", n_assert);
    def(vm, "clock", "() -> float", n_clock);
    def(vm, "defined", "(string name) -> bool", n_defined);
}
