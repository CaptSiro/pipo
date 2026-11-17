// extern functions: C functions of a DLL (md/EXTERN.md). Uses the C runtime DLLs every Windows has.
import io
import c
import types

extern "msvcrt" function abs(c:i32 x) -> c:i32
extern "msvcrt" "labs" function long_abs(c:i32 x) -> c:i32
extern "msvcrt" function strlen(string s) -> c:u64
extern "msvcrt" function pow(float x, float y) -> float
extern "msvcrt" function atof(string s) -> float
extern "msvcrt" function strtol(string s, c:ptr end, c:i32 base) -> c:i32
extern "msvcrt" function toupper(c:i32 ch) -> c:i32
extern "msvcrt" function strchr(string s, c:i32 ch) -> string
extern "msvcrt" function memset(types:Int8Array dst, c:i32 value, c:u64 count) -> c:ptr
extern "msvcrt" function strncpy(types:Int8Array dst, string src, c:u64 count) -> c:ptr
extern "ucrtbase" function powf(c:f32 x, c:f32 y) -> c:f32
extern "ucrtbase" function fmaf(c:f32 x, c:f32 y, c:f32 z) -> c:f32
extern "ucrtbase.dll" function fma(float x, float y, float z) -> float

io:print_nl(abs(-42))
io:print_nl(long_abs(-7))
io:print_nl(strlen("hello"))
io:print_nl(strlen("ž"))                 // bytes, not characters
io:print_nl(pow(2, 10))
io:print_nl(atof("2.5"))
io:print_nl(strtol("ff", 0, 16))
io:print_nl(chr(toupper(ord("q"))))
io:print_nl(strchr("hello", ord("l")))
io:print_nl(strchr("hello", ord("z")))    // NULL comes back as ""

const buf = types:int8array(4)
memset(buf, 7, 3)
io:print_nl(buf)
strncpy(buf, "abcd", 4)
io:print_nl(buf.text())

io:print_nl(powf(1.5, 2))
io:print_nl(fmaf(2, 3, 0.5))
io:print_nl(fma(2, 3, 0.5))

// externs are functions like any other: values, dot calls, overloads by PiPo type
io:print_nl([-1, 2, -3].map(x -> abs(x)).to_array())
io:print_nl("abc".strlen())
