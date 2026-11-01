import io
import io -> console
import io -> { print, write }
import "./modules/mathx"
import "./modules/mathx" -> m

io:print("io:print")
console:print("aliased module behaves like an object:", console)
print("destructured print")
write("no newline")
print()

// receiver.module:function(args) == module:function(receiver, args)
io:print(4.mathx:add(mathx:factor))
io:print(mathx:add(4, mathx:factor))
io:print(m:point(1, 2).m:double())

// module types in annotations, module enums in patterns
function describe(mathx:Point p) -> string {
    return "(" + str(p:x) + ", " + str(p:y) + ")"
}
const mathx:Point origin = { x: 0, y: 0 }
io:print(origin.describe(), type_of(origin))

const c = mathx:Color:GREEN
io:print(match c {
    mathx:Color:RED -> "red"
    mathx:Color:GREEN -> "green"
})

// attributes use ':'
const obj = { name: "obj", nested: { value: 5 }, greet: function(string who) { return "hi " + who } }
obj:nested:value += 1
obj["extra"] = true
io:print(obj:name, obj:nested:value, obj:greet("bob"), obj:extra)

// files
const path = "pp_io_test.tmp"
match io:write_file(path, "line one\nline two") {
    success(n) -> io:print("wrote", n, "bytes")
    fail(reason) -> io:print("write failed:", reason)
}
match io:read_file(path) {
    success(text) -> io:print(split(text, "\n"))
    fail(reason) -> io:print("read failed:", reason)
}
io:print(io:read_file("definitely/missing/file.txt"))

// module constants
io:write("const nl" + io:nl)
io:print(type_of(io:nl), io:nl == "
")
