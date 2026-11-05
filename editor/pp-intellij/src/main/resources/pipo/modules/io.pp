// The io module (src/modules/io.c). A stub bundled with the IDE plugin: it only
// describes the signatures. Use it as `import io`, then io:print(...).

// the constant "\n": io:print("done" + io:nl)
export const string nl = "\n"

// prints the arguments separated by spaces, without a newline; objects as pretty JSON
export native function print(...)
// prints the arguments separated by spaces, then a newline
export native function print_nl(...)
// like print, to stderr
export native function eprint(...)
// like print_nl, to stderr
export native function eprint_nl(...)
// prints the arguments as a prompt, then reads one line from stdin (without the newline)
export native function input(...) -> string
// the whole contents of a file
export native function read_file(string path) -> Error<string>
// replaces the contents of a file; success(number of bytes written)
export native function write_file(string path, string content) -> Error<int>
