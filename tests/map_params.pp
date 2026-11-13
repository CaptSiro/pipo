import io

// a record where a Map<string, V> is expected becomes a map of its fields
// the bare-bones element: any attributes, any children
function element(Map<string, unknown> attrs) -> string {
    return element(attrs, [])
}

function element(Map<string, unknown> attrs, unknown[] children) -> string {
    let s = "<"
    if some(t) = attrs.get("tag") {
        s = s + str(t)
    }

    for k, v in attrs {
        if k != "tag" {
            s = s + " " + k + "=" + str(v)
        }
    }

    s = s + ">"
    for c in children {
        s = s + str(c)
    }

    return s
}

io:print_nl(<element tag="my-widget" size={3} data-user-id="u1"/>)
io:print_nl(<element tag="p" class="x">"hello " {42} <element tag="b"/></element>)
io:print_nl(element({ tag: "span", n: 1 }))

function count(Map<string, int> m) -> int {
    let total = 0
    for k, v in m {
        total += v
    }

    return total
}

io:print_nl(count({ a: 1, b: 2 }))
const Map<string, int> m = { x: 5, y: 6 }
io:print_nl(m, count(m))
const r = { p: 7 }
io:print_nl(count(r))
type Holder = { Map<string, string> attrs }
const h = Holder{ attrs: { a: "1" } }
io:print_nl(h:attrs.get("a"))
function mk() -> Map<string, bool> {
    return { on: true }
}

io:print_nl(mk())

// a literal inside a literal becomes what its field expects
function first_attr(Holder h) -> string? {
    return h:attrs.get("a")
}

io:print_nl(first_attr({ attrs: { a: "1" } }))
