import io
import io -> console
import io -> { print_nl, print }
import "./modules/mathx"
import "./modules/mathx" -> m

io:print_nl("io:print_nl")
console:print_nl("aliased module behaves like an object:", console)
print_nl("destructured print")
print("no newline")
print_nl()

// receiver.module:function(args) == module:function(receiver, args)
io:print_nl(4.mathx:add(mathx:factor))
io:print_nl(mathx:add(4, mathx:factor))
io:print_nl(m:point(1, 2).m:double())

// module types in annotations, module enums in patterns
function describe(mathx:Point p) -> string {
    return "(" + str(p:x) + ", " + str(p:y) + ")"
}
const mathx:Point origin = { x: 0, y: 0 }
io:print_nl(origin.describe(), type_of(origin))

const c = mathx:Color:GREEN
io:print_nl(match c {
    mathx:Color:RED -> "red"
    mathx:Color:GREEN -> "green"
})

// attributes use ':'
const obj = { name: "obj", nested: { value: 5 }, greet: function(string who) { return "hi " + who } }
obj:nested:value += 1
obj["extra"] = true
io:print_nl(obj:name, obj:nested:value, obj:greet("bob"), obj:extra)

// files
const path = "pp_io_test.tmp"
match io:write_file(path, "line one\nline two") {
    success(n) -> io:print_nl("wrote", n, "bytes")
    fail(reason) -> io:print_nl("write failed:", reason)
}
match io:read_file(path) {
    success(text) -> io:print_nl(split(text, "\n"))
    fail(reason) -> io:print_nl("read failed:", reason)
}
io:print_nl(io:read_file("definitely/missing/file.txt"))

// module constants
io:print("const nl" + io:nl)
io:print_nl(type_of(io:nl), io:nl == "
")
