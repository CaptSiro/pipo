import io

function half(int x) -> Error<int> {
    if x % 2 != 0 { return fail("odd: " + str(x)) }
    return success(x / 2)
}

function quarter(int x) -> Error<int> {
    const h = half(x).lift()
    const q = lift(half(h))
    io:print_nl("quarter ok " + str(q))
    return success(q)
}

function first_float(string[] xs) -> float? {
    for x in xs {
        const f = parse_float(x).lift()
        if f > 10 { return some(f) }
    }
    return none()
}

function sum_halves(int[] xs) -> Error<int> {
    let total = 0
    for x in xs {
        total += half(x).lift()
    }
    return success(total)
}

function lam() -> int[] {
    return [1, 2, 3].map(x -> (x * 2).some().lift())
}

io:print_nl(quarter(8))
io:print_nl(quarter(6))
io:print_nl(quarter(5))
io:print_nl(first_float(["1.5", "20"]))
io:print_nl(first_float(["1.5", "abc", "20"]))
io:print_nl(sum_halves([2, 4, 6]))
io:print_nl(sum_halves([2, 3, 6]))
io:print_nl(lam())
for i in 0...2000 { quarter(6) }
io:print_nl("loop ok")
