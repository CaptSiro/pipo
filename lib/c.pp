// ---- c: the C types of extern functions (md/EXTERN.md)
//
//     import c
//     extern "glfw3" function glfwInit() -> c:i32
//     extern "opengl32" function glClearColor(c:f32 r, c:f32 g, c:f32 b, c:f32 a) -> void
//
// Each type is an alias, so the program sees a plain int or float. Only an extern declaration
// reads the name: it decides how wide the value is when it crosses into C, and how a result is
// read back. An int that does not fit the narrow type is a runtime error, not a silent wrap.
//
// Without a c: type, int is int64_t, float is double, bool is an int 0 / 1 (a returned bool is
// a C int, non-zero is true), string is a NUL-terminated const char*, string[] a const char**,
// a types:*Array a pointer to its elements (C may write into them), and string? or a
// types:*Array? passes none() as NULL.

import types

export type i8 = int
export type u8 = int
export type i16 = int
export type u16 = int
export type i32 = int
export type u32 = int
export type i64 = int
// the bits of the int: a u64 above the largest int comes back negative
export type u64 = int
export type f32 = float
export type f64 = float
// an address, 0 is NULL: window handles, contexts, offsets into a bound buffer
export type ptr = int

export type Int32Pointer = types:Int32Array
// the same 4 bytes read as unsigned (GLuint ids): its own tag, so * gives a u32, not an i32
export type UInt32Pointer = types:Int32Array
export type Int64Pointer = types:Int64Array



export function chars(types:Int8Array bytes) -> string {
    for i in ...#bytes {
        if bytes[i] == 0 {
            return bytes.text(0, i)
        }
    }

    return bytes.text(0, #bytes)
}



export function int32pointer() -> Int32Pointer {
    return types:int32array(1)
}

export global function *(Int32Pointer ptr) -> i32 {
    return ptr[0]
}

export global function *=(Int32Pointer ptr, i32 value) -> i32 {
    ptr[0] = value

    return value
}

export global function &(i32 number) -> Int32Pointer {
    const ptr = int32pointer()
    ptr *= number

    return ptr
}

export function uint32pointer() -> UInt32Pointer {
    return types:int32array(1)
}

// an Int32Array holds the bits as signed: -1 is 4294967295
export global function *(UInt32Pointer ptr) -> u32 {
    const int bits = ptr[0]

    return if bits < 0 {
        bits + 4294967296
    } else {
        bits
    }
}

export global function *=(UInt32Pointer ptr, u32 value) -> u32 {
    ptr[0] = if value > 2147483647 {
        value - 4294967296
    } else {
        value
    }

    return value
}

export global function &(u32 number) -> UInt32Pointer {
    const ptr = uint32pointer()
    ptr *= number

    return ptr
}

export function int64pointer() -> Int64Pointer {
    return types:int64array(1)
}

export global function *(Int64Pointer ptr) -> i64 {
    return ptr[0]
}

export global function *=(Int64Pointer ptr, i64 value) -> i64 {
    ptr[0] = value

    return value
}

export global function &(i64 number) -> Int64Pointer {
    const ptr = int64pointer()
    ptr *= number

    return ptr
}
