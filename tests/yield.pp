import io
// `yield` gives the value of a match arm / scope call block; it must be the block's last statement.
// An if is exempt: like a ternary, its branches give their last expression.

const n = 7
const parity = if n % 2 == 0 { "even" } else { "odd" }
io:print_nl(parity)

const label = match n {
    0 -> "zero"
    x if x > 5 -> {
        const big = x * 100
        yield "big " + str(big)
    }
    _ -> "small"
}
io:print_nl(label)

// nested: the if is yielded as a whole
const sign = match n {
    0 -> 0
    _ -> {
        yield if n > 0 { 1 } else { -1 }
    }
}
io:print_nl(sign)

// a scope's code() returns what the block yields
scope _with(Block<int> code, int start) -> int {
    return code(start) + 1
}

const v = _with 2 {
    let total = 0
    for i in ...scope { total += i + 10 }
    yield total
}
io:print_nl(v)

// without yield the block gives nothing; at statement level that is fine
_with 3 {
    io:print_nl("scope is", scope)
    yield 0
}

// lambdas: an expression body is the result, a block body uses return
type Twice = int -> int
const Twice twice = x -> x * 2
const Twice twice_block = x -> {
    const d = x * 2
    return d
}
io:print_nl(twice(4), twice_block(5))

// a statement-level if gives a value nobody uses
if n > 0 {
    n
}
io:print_nl("done")
