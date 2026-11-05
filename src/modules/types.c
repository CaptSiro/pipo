/*
 * The C half of lib/types.pp: typed arrays.
 *
 *     import types
 *     const buf = types:int8array(1024)       // 1024 zeros; the length never changes
 *     buf[0] = 65
 *     buf[1] = 300                            // error: 300 does not fit in Int8Array
 *
 * Int8Array ... Int64Array hold ints of that many bits, Float32Array and
 * Float64Array hold floats. Elements are stored as raw C numbers, so the
 * arrays are compact and are what fs reads file contents into. Unlike T[],
 * a typed array checks every value written to it: an Int*Array only takes
 * ints that fit its element type, a Float*Array takes ints and floats.
 *
 * The types (PascalCase) are not callable; values come from the constructor
 * functions int8array ... float64array (flatcase).
 */
#include <float.h>
#include <math.h>

#include "vm.h"

typedef struct {
    const char* name;
    const char* ctor; /* its constructor function in types.pp */
    size_t size;      /* bytes per element */
    bool is_float;
    int64_t min, max; /* int kinds: the values that fit */
} KindInfo;

static const KindInfo KINDS[] = {
    [TA_I8] = {"Int8Array", "int8array", 1, false, INT8_MIN, INT8_MAX},
    [TA_I16] = {"Int16Array", "int16array", 2, false, INT16_MIN, INT16_MAX},
    [TA_I32] = {"Int32Array", "int32array", 4, false, INT32_MIN, INT32_MAX},
    [TA_I64] = {"Int64Array", "int64array", 8, false, INT64_MIN, INT64_MAX},
    [TA_F32] = {"Float32Array", "float32array", 4, true, 0, 0},
    [TA_F64] = {"Float64Array", "float64array", 8, true, 0, 0},
};

#define TA(o) ((ObjTypedArray*)(o))

/* ---------------------------------------------------------- element access */

static Value elem_get(ObjTypedArray* a, int64_t i) {
    switch (a->kind) {
        case TA_I8: return int_val(((int8_t*)a->data)[i]);
        case TA_I16: return int_val(((int16_t*)a->data)[i]);
        case TA_I32: return int_val(((int32_t*)a->data)[i]);
        case TA_I64: return int_val(((int64_t*)a->data)[i]);
        case TA_F32: return float_val(((float*)a->data)[i]);
        case TA_F64: return float_val(((double*)a->data)[i]);
    }
    return void_val();
}

/*
 * The value v converted for an element of kind k: a runtime error when it is
 * not a number of the right kind or does not fit. `what` names the target in
 * the message ("Int8Array", "Int8Array element 3").
 */
static Value check_elem(VM* vm, TypedArrayKind k, Value v, const char* what, int line) {
    const KindInfo* info = &KINDS[k];
    if (info->is_float) {
        if (v.kind != VAL_INT && v.kind != VAL_FLOAT) rt_error(vm, line, "%s holds floats, got %s", what, value_type_name(v));
        double d = num_as_double(v);
        if (k == TA_F32 && isfinite(d) && fabs(d) > FLT_MAX) rt_error(vm, line, "%g does not fit in %s", d, what);
        return float_val(d);
    }
    if (v.kind != VAL_INT) rt_error(vm, line, "%s holds ints, got %s", what, value_type_name(v));
    if (v.as.i < info->min || v.as.i > info->max)
        rt_error(vm, line, "%" PRId64 " does not fit in %s (%" PRId64 "...=%" PRId64 ")", v.as.i, what, info->min, info->max);
    return v;
}

/* stores a value already checked by check_elem */
static void elem_put(ObjTypedArray* a, int64_t i, Value v) {
    switch (a->kind) {
        case TA_I8: ((int8_t*)a->data)[i] = (int8_t)v.as.i; break;
        case TA_I16: ((int16_t*)a->data)[i] = (int16_t)v.as.i; break;
        case TA_I32: ((int32_t*)a->data)[i] = (int32_t)v.as.i; break;
        case TA_I64: ((int64_t*)a->data)[i] = v.as.i; break;
        case TA_F32: ((float*)a->data)[i] = (float)v.as.f; break;
        case TA_F64: ((double*)a->data)[i] = v.as.f; break;
    }
}

static void elem_set(VM* vm, ObjTypedArray* a, int64_t i, Value v, int line) {
    elem_put(a, i, check_elem(vm, a->kind, v, KINDS[a->kind].name, line));
}

static int64_t index_of(VM* vm, ObjTypedArray* a, Value idx, int line) {
    if (idx.kind != VAL_INT) rt_error(vm, line, "index must be an int, got %s", value_type_name(idx));
    int64_t i = idx.as.i;
    if (i < 0) i += a->length;
    if (i < 0 || i >= a->length)
        rt_error(vm, line, "index %" PRId64 " out of range (%s of length %" PRId64 ")", idx.as.i, KINDS[a->kind].name,
                 a->length);
    return i;
}

/* ------------------------------------------------------------ the class */

static void ta_free(ObjInstance* self);

bool is_typed_array(Value v) { return IS_KIND(v, OBJ_INSTANCE) && AS_INSTANCE(v)->type->cls->free == ta_free; }

ObjTypedArray* typed_array_new(VM* vm, TypedArrayKind kind, int64_t length) {
    if (length < 0) rt_error(vm, 0, "%s length must not be negative, got %" PRId64, KINDS[kind].name, length);
    size_t esize = KINDS[kind].size;
    if ((uint64_t)length > SIZE_MAX / esize / 2)
        rt_error(vm, 0, "%s of length %" PRId64 " is too large", KINDS[kind].name, length);
    void* data = calloc(length ? (size_t)length : 1, esize);
    if (!data) rt_error(vm, 0, "out of memory allocating %s of length %" PRId64, KINDS[kind].name, length);
    ObjTypedArray* a = (ObjTypedArray*)instance_new(vm, vm_native_type(vm, "types", KINDS[kind].name));
    a->kind = kind;
    a->length = length;
    a->data = data;
    return a;
}

/*
 * int8array(16): 16 zeros. int8array([1, 2, 3]) and int8array(other) copy the
 * values, each checked like an element assignment. types.pp only lets an int,
 * a number[] or a typed array through.
 */
static Value ta_make(VM* vm, TypedArrayKind kind, Value from) {
    const char* name = KINDS[kind].name;
    int line = 0;
    if (from.kind == VAL_INT) return obj_val(typed_array_new(vm, kind, from.as.i));

    char what[64];
    if (IS_KIND(from, OBJ_ARRAY)) {
        ObjArray* src = AS_ARRAY(from);
        ObjTypedArray* a = typed_array_new(vm, kind, src->count);
        for (int i = 0; i < src->count; i++) {
            snprintf(what, sizeof what, "%s element %d", name, i);
            elem_put(a, i, check_elem(vm, kind, src->items[i], what, line));
        }
        return obj_val(a);
    }
    if (is_typed_array(from)) {
        ObjTypedArray* src = TA(from.as.o);
        ObjTypedArray* a = typed_array_new(vm, kind, src->length);
        if (src->kind == kind) {
            memcpy(a->data, src->data, (size_t)src->length * KINDS[kind].size);
            return obj_val(a);
        }
        if (!KINDS[kind].is_float && KINDS[src->kind].is_float)
            rt_error(vm, line, "%s holds ints, so it cannot be made from a %s", name, KINDS[src->kind].name);
        for (int64_t i = 0; i < src->length; i++) {
            snprintf(what, sizeof what, "%s element %" PRId64, name, i);
            elem_put(a, i, check_elem(vm, kind, elem_get(src, i), what, line));
        }
        return obj_val(a);
    }
    rt_error(vm, line, "%s(...) takes a length (int), an array or a typed array, got %s", KINDS[kind].ctor,
             value_type_name(from));
}

static Value ta_new_i8(VM* vm, int argc, Value* a) { return ta_make(vm, TA_I8, a[0]); }
static Value ta_new_i16(VM* vm, int argc, Value* a) { return ta_make(vm, TA_I16, a[0]); }
static Value ta_new_i32(VM* vm, int argc, Value* a) { return ta_make(vm, TA_I32, a[0]); }
static Value ta_new_i64(VM* vm, int argc, Value* a) { return ta_make(vm, TA_I64, a[0]); }
static Value ta_new_f32(VM* vm, int argc, Value* a) { return ta_make(vm, TA_F32, a[0]); }
static Value ta_new_f64(VM* vm, int argc, Value* a) { return ta_make(vm, TA_F64, a[0]); }

static void ta_free(ObjInstance* self) { free(TA(self)->data); }

/* a float as short as possible while still reading back as the same float32: 0.1, not 0.10000000149011612 */
static void format_f32(StrBuf* sb, float f) {
    if (isnan(f) || isinf(f)) {
        format_float(sb, f);
        return;
    }
    char buf[48];
    for (int prec = 1; prec <= 9; prec++) {
        snprintf(buf, sizeof buf, "%.*g", prec, (double)f);
        if ((float)strtod(buf, NULL) == f) break;
    }
    /* keep floats looking like floats, as format_float does: 1.0, not 1 */
    if (!strpbrk(buf, ".eE")) strcat(buf, ".0");
    sb_puts(sb, buf);
}

/* Int8Array[1, 2, 3] */
static void ta_format(StrBuf* sb, ObjInstance* self) {
    ObjTypedArray* a = TA(self);
    sb_puts(sb, KINDS[a->kind].name);
    sb_putc(sb, '[');
    for (int64_t i = 0; i < a->length; i++) {
        if (i) sb_puts(sb, ", ");
        if (a->kind == TA_F32) format_f32(sb, ((float*)a->data)[i]);
        else if (a->kind == TA_F64) format_float(sb, ((double*)a->data)[i]);
        else sb_printf(sb, "%" PRId64, elem_get(a, i).as.i);
    }
    sb_putc(sb, ']');
}

/* the same kind (the caller checked the type), length and elements */
static bool ta_equals(ObjInstance* x, ObjInstance* y) {
    ObjTypedArray* a = TA(x);
    ObjTypedArray* b = TA(y);
    if (a->length != b->length) return false;
    if (!KINDS[a->kind].is_float) return memcmp(a->data, b->data, (size_t)a->length * KINDS[a->kind].size) == 0;
    for (int64_t i = 0; i < a->length; i++)
        if (elem_get(a, i).as.f != elem_get(b, i).as.f) return false;
    return true;
}

static ObjInstance* ta_copy(VM* vm, ObjInstance* self) {
    ObjTypedArray* a = TA(self);
    ObjTypedArray* r = typed_array_new(vm, a->kind, a->length);
    memcpy(r->data, a->data, (size_t)a->length * KINDS[a->kind].size);
    return (ObjInstance*)r;
}

static int64_t ta_length(ObjInstance* self) { return TA(self)->length; }

static Value ta_get(VM* vm, ObjInstance* self, Value idx, int line) {
    ObjTypedArray* a = TA(self);
    return elem_get(a, index_of(vm, a, idx, line));
}

static void ta_set(VM* vm, ObjInstance* self, Value idx, Value v, int line) {
    ObjTypedArray* a = TA(self);
    elem_set(vm, a, index_of(vm, a, idx, line), v, line);
}

#define TA_CLASS(type_name, ctor_name)                                                                             \
    {.name = type_name, .size = sizeof(ObjTypedArray), .constructor = "types:" ctor_name, .free = ta_free,           \
     .format = ta_format, .equals = ta_equals, .copy = ta_copy, .length = ta_length, .get = ta_get, .set = ta_set}

static const NativeClass TYPES[] = {
    TA_CLASS("Int8Array", "int8array"),
    TA_CLASS("Int16Array", "int16array"),
    TA_CLASS("Int32Array", "int32array"),
    TA_CLASS("Int64Array", "int64array"),
    TA_CLASS("Float32Array", "float32array"),
    TA_CLASS("Float64Array", "float64array"),
    {.name = NULL},
};

/* ------------------------------------------------- functions of types.pp */

/* start / end like slice: negative counts from the end, then clamped to 0...=length */
static void clamp_range(int64_t len, int64_t* start, int64_t* end) {
    if (*start < 0) *start += len;
    if (*end < 0) *end += len;
    if (*start < 0) *start = 0;
    if (*end > len) *end = len;
    if (*start > *end) *start = *end;
}

/* fill(a, value), fill(a, value, start, end) */
static Value ta_fill(VM* vm, int argc, Value* a) {
    ObjTypedArray* arr = TA(a[0].as.o);
    Value v = check_elem(vm, arr->kind, a[1], KINDS[arr->kind].name, 0);
    int64_t start = 0, end = arr->length;
    if (argc > 2) {
        start = a[2].as.i;
        end = a[3].as.i;
        clamp_range(arr->length, &start, &end);
    }
    for (int64_t i = start; i < end; i++) elem_put(arr, i, v);
    return void_val();
}

/* slice(a, start), slice(a, start, end): a new typed array of the same kind */
static Value ta_slice(VM* vm, int argc, Value* a) {
    ObjTypedArray* arr = TA(a[0].as.o);
    int64_t start = a[1].as.i, end = argc > 2 ? a[2].as.i : arr->length;
    clamp_range(arr->length, &start, &end);
    ObjTypedArray* r = typed_array_new(vm, arr->kind, end - start);
    size_t es = KINDS[arr->kind].size;
    memcpy(r->data, (char*)arr->data + (size_t)start * es, (size_t)(end - start) * es);
    return obj_val(r);
}

/* to_array(a) -> int[] / float[] */
static Value ta_to_array(VM* vm, int argc, Value* a) {
    ObjTypedArray* arr = TA(a[0].as.o);
    if (arr->length > INT32_MAX) rt_error(vm, 0, "%s is too long for an array", KINDS[arr->kind].name);
    ObjArray* r = array_new(vm, (int)arr->length);
    for (int64_t i = 0; i < arr->length; i++) r->items[r->count++] = elem_get(arr, i);
    return obj_val(r);
}

/*
 * copy_into(src, src_pos, dst, dst_pos, length): like Java's System.arraycopy.
 * Both arrays must be of the same kind; the ranges may overlap.
 */
static Value ta_copy_into(VM* vm, int argc, Value* a) {
    ObjTypedArray* src = TA(a[0].as.o);
    int64_t sp = a[1].as.i;
    ObjTypedArray* dst = TA(a[2].as.o);
    int64_t dp = a[3].as.i, n = a[4].as.i;
    if (src->kind != dst->kind)
        rt_error(vm, 0, "copy_into needs two arrays of the same kind, got %s and %s", KINDS[src->kind].name,
                 KINDS[dst->kind].name);
    if (n < 0 || sp < 0 || dp < 0 || sp > src->length - n || dp > dst->length - n)
        rt_error(vm, 0,
                 "copy_into(src, %" PRId64 ", dst, %" PRId64 ", %" PRId64 ") is out of range (src length %" PRId64
                 ", dst length %" PRId64 ")",
                 sp, dp, n, src->length, dst->length);
    size_t es = KINDS[src->kind].size;
    memmove((char*)dst->data + (size_t)dp * es, (char*)src->data + (size_t)sp * es, (size_t)n * es);
    return void_val();
}

/* bytes(text) -> Int8Array: the UTF-8 bytes of a string */
static Value ta_bytes(VM* vm, int argc, Value* a) {
    ObjString* s = AS_STR(a[0]);
    ObjTypedArray* r = typed_array_new(vm, TA_I8, s->len);
    memcpy(r->data, s->chars, (size_t)s->len);
    return obj_val(r);
}

/* text(bytes), text(bytes, start, end) -> string: bytes read as UTF-8 text */
static Value ta_text(VM* vm, int argc, Value* a) {
    ObjTypedArray* arr = TA(a[0].as.o);
    int64_t start = 0, end = arr->length;
    if (argc > 1) {
        start = a[1].as.i;
        end = a[2].as.i;
        clamp_range(arr->length, &start, &end);
    }
    if (end - start > INT32_MAX) rt_error(vm, 0, "text(...): %" PRId64 " bytes is too long for a string", end - start);
    return obj_val(str_new(vm, (char*)arr->data + start, (int)(end - start)));
}

/* element_size(a) -> int: bytes per element (1 for Int8Array, 8 for Float64Array) */
static Value ta_element_size(VM* vm, int argc, Value* a) { return int_val((int64_t)KINDS[TA(a[0].as.o)->kind].size); }

static const NativeImpl FUNCTIONS[] = {
    {"int8array", ta_new_i8},
    {"int16array", ta_new_i16},
    {"int32array", ta_new_i32},
    {"int64array", ta_new_i64},
    {"float32array", ta_new_f32},
    {"float64array", ta_new_f64},
    {"ta_fill", ta_fill},
    {"ta_slice", ta_slice},
    {"ta_to_array", ta_to_array},
    {"copy_into", ta_copy_into},
    {"bytes", ta_bytes},
    {"text", ta_text},
    {"element_size", ta_element_size},
    {NULL, NULL},
};

void mod_types_register(VM* vm) {
    vm_register_native_types(vm, "types", TYPES);
    vm_register_native_impls(vm, "types", FUNCTIONS);
}
