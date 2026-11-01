interface Str {
    function str(Str) -> string
}
function show(Str s) -> string {
    return s.str()
}
show(42)
