interface Str {
    function str(Str) -> string
}

// interfaces are closed: they cannot be reopened to add functions
interface Str {
    function name(Str) -> string
}
