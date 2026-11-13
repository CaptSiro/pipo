import io

// `..` would turn `x..f()` into `x .. f()`
function ..(int a, int b) -> int {
    return a * b
}
