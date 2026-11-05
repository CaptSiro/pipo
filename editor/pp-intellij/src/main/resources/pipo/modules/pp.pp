// The pp module (src/modules/pp.c): information about the running script. A
// stub bundled with the IDE plugin: it only describes the signatures.

// the path of the file that calls it
export native function file() -> string
// the directory of the file that calls it
export native function dir() -> string
// the line that calls it
export native function line() -> int
