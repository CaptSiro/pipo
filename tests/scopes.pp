// scopes: user defined blocks `_name args { ... }`
import io

interface Resource {
    function close(Resource) -> void
}

type Res = { string name }

function close(Res r) -> void {
    io:print_nl("close", r:name)
}

function open(string name) -> Res {
    io:print_nl("open", name)
    return Res({ name: name })
}

impl Resource for Res

scope _resource(Block<void> code, Resource r) -> void {
    defer { r.close() }
    code()
}

// binding like `if some(x) = ...`: f only exists in the block
_resource const f = open("a") {
    io:print_nl("using", f:name)
}

// non-local return: leaves the enclosing function, the defer still runs
function first_line() -> string {
    _resource const f = open("b") {
        return "from " + f:name
    }

    return "unreachable"
}

io:print_nl(first_line())

// break and continue reach the loop around the scope call
for i in ...4 {
    _resource const f = open("loop" + str(i)) {
        if i == 1 {
            continue
        }

        if i == 2 {
            break
        }

        io:print_nl("body", i)
    }
}

// the value the block is called with is `scope`
scope _times(Block<int> code, int n) -> void {
    for i in ...n {
        code(i)
    }
}

_times 3 {
    io:print_nl("time", scope)
}

// nested: save the outer value to reach it from the inner block
_times 2 {
    const outer = scope
    _times 2 {
        io:print_nl(outer, scope)
    }
}

// defer is replaced: the last one before the exit runs
scope _steps(Block<string> code) -> void {
    defer { io:print_nl("cleanup 1") }
    code("one")
    defer { io:print_nl("cleanup 2") }
    code("two")
    defer { io:print_nl("cleanup 3") }
}

function steps(bool stop) -> void {
    _steps {
        io:print_nl("step", scope)
        if stop && scope == "two" {
            return
        }
    }

    io:print_nl("steps done")
}

steps(false)
steps(true)

// `yield` gives the result of code(), the scope's result is the call's value
scope _twice(Block<int> code, int x) -> int {
    return code(code(x))
}

io:print_nl(_twice 3 { yield scope * 10 })

// lift in a block returns from the enclosing function, after the cleanups
function parse_both(string a, string b) -> Error<int> {
    _resource const f = open("parse") {
        const x = lift(parse_age(a))
        const y = lift(parse_age(b))
        return success(x + y)
    }

    return fail("unreachable")
}

function parse_age(string s) -> Error<int> {
    if some(n) = parse_int(s) {
        return success(n)
    }

    return fail("not a number: " + s)
}

io:print_nl(parse_both("1", "2"))
io:print_nl(parse_both("1", "x"))

// a Callback can be kept and called after the scope call ended
let Callback<string>[] handlers = []

scope _on(Callback<string> code, string event) -> void {
    handlers.push(code)
}

_on "click" {
    io:print_nl("handled", scope)
}

for h in handlers {
    h("click!")
}

// scopes are ordinary overload sets: the arguments choose
scope _show(Block<string> code, int n) -> void {
    code("int " + str(n))
}

scope _show(Block<string> code, string s) -> void {
    code("string " + s)
}

_show 1 { io:print_nl(scope) }
_show "x" { io:print_nl(scope) }

// parentheses work too
_show(2) { io:print_nl(scope) }
