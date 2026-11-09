scope _x(Block<void> code) -> int {
    code()
    return 1
}
function f() -> int {
    const v = _x {
        return 2
    }
    return v
}
f()
