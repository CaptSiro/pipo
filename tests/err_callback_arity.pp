type Pair = (int, int) -> int
function apply(Pair f) -> int {
    return f(1, 2)
}
apply(x -> x)
