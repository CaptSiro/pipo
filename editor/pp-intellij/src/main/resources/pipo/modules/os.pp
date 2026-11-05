// The os module (src/modules/os.c). A stub bundled with the IDE plugin: it only
// describes the signatures.

// the platform name the interpreter was built for
export const string platform = ""

// the script path and everything after it on the command line
export native function args() -> string[]
// joins path parts with the platform's separator: os:join("data", "order.txt") == "data\order.txt" on Windows
export native function join(...) -> string
// os:join(pp:dir(), parts...): relative to the file the call is written in
export native function join_pp(...) -> string
