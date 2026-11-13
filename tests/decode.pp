import io

// decode(m, T): a Map<string, unknown> checked and converted into the record type T, as an Error<T>
type Card = { string title, int? size, float weight, ...Map<string, unknown> extra }
type Page = { string name, Card card }

const ok = decode(map({ title: "Hi", weight: 2, size: 3, color: "red" }), Card)
match ok {
    success(c) -> io:print_nl(c:title, c:size, c:weight, c:extra)
    fail(why) -> io:print_nl("fail", why)
}

const Map<string, unknown> raw = map({ name: "home", card: map({ title: 5, weight: 1.0 }) })
match decode(raw, Page) {
    success(p) -> io:print_nl(p:name)
    fail(why) -> io:print_nl(why)
}

match decode(map({ weight: 1.5 }), Card) {
    success(c) -> io:print_nl(c)
    fail(why) -> io:print_nl(why)
}

type Strict = { int a }
io:print_nl(decode(map({ a: 1, b: 2 }), Strict), decode(map({ a: 1 }), Strict))
