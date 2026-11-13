import io
// next() of a generator from its own body is an error
let Generator<int>? self = none()
generator selfish() -> int {
    emit 1
    match self {
        some(g) -> { g.next() }
        none() -> {}
    }
}

const s = selfish()
self = some(s)
io:print_nl(s.next())
s.next()
