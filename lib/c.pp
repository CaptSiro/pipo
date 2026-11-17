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
