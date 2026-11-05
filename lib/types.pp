// ---- Typed arrays: fixed-length arrays of raw numbers, written in C (src/modules/types.c)
// The types are not callable: int8array(length) ... float64array(length) make values (flatcase
// constructors, unlike the snake_case of the other modules). The length never changes and the
// elements start at 0.
//
//     const buf = types:int8array(4)        // Int8Array[0, 0, 0, 0]
//     buf[0] = 127                          // buf[0] = 128 is an error: it does not fit
//     types:float32array([1, 2.5])          // copies an array (or another typed array)
//
// Int8Array ... Int64Array hold ints of 8 to 64 bits and only take ints that fit,
// Float32Array and Float64Array hold floats and also take ints. #a, a[i], a[i] = v
// (negative i counts from the end), for x in a, for i, x in a, ==, copy(a) and
// printing work like on arrays.
export native type Int8Array
export native type Int16Array
export native type Int32Array
export native type Int64Array
export native type Float32Array
export native type Float64Array

export type IntArray = Int8Array || Int16Array || Int32Array || Int64Array
export type FloatArray = Float32Array || Float64Array
export type TypedArray = IntArray || FloatArray

// ---- constructors: a length (zeros), or the values of an array / typed array, each checked
export native function int8array(int length) -> Int8Array
export native function int8array(number[] values) -> Int8Array
export native function int8array(TypedArray values) -> Int8Array
export native function int16array(int length) -> Int16Array
export native function int16array(number[] values) -> Int16Array
export native function int16array(TypedArray values) -> Int16Array
export native function int32array(int length) -> Int32Array
export native function int32array(number[] values) -> Int32Array
export native function int32array(TypedArray values) -> Int32Array
export native function int64array(int length) -> Int64Array
export native function int64array(number[] values) -> Int64Array
export native function int64array(TypedArray values) -> Int64Array
export native function float32array(int length) -> Float32Array
export native function float32array(number[] values) -> Float32Array
export native function float32array(TypedArray values) -> Float32Array
export native function float64array(int length) -> Float64Array
export native function float64array(number[] values) -> Float64Array
export native function float64array(TypedArray values) -> Float64Array

// a.fill(v) sets every element, a.fill(v, start, end) the ones in [start, end)
export native "ta_fill" function fill(IntArray a, int value) -> void
export native "ta_fill" function fill(FloatArray a, float value) -> void
export native "ta_fill" function fill(IntArray a, int value, int start, int end) -> void
export native "ta_fill" function fill(FloatArray a, float value, int start, int end) -> void

// a new typed array of the same kind; start and end work like slice on arrays
export native "ta_slice" function slice(TypedArray a, int start) -> TypedArray
export native "ta_slice" function slice(TypedArray a, int start, int end) -> TypedArray

// the elements as a plain array
export native "ta_to_array" function to_array(IntArray a) -> int[]
export native "ta_to_array" function to_array(FloatArray a) -> float[]

// like Java's System.arraycopy: length elements from src[src_pos] to dst[dst_pos].
// Both must be of the same kind; the ranges may overlap.
export native function copy_into(TypedArray src, int src_pos, TypedArray dst, int dst_pos, int length) -> void

// bytes per element: 1 for Int8Array, 8 for Int64Array and Float64Array
export native function element_size(TypedArray a) -> int

// strings and bytes: bytes(s) is the UTF-8 encoding of s, text(b) reads bytes back as a string
export native function bytes(string text) -> Int8Array
export native function text(Int8Array bytes) -> string
export native function text(Int8Array bytes, int start, int end) -> string
