import io
import "./modules/ui":shapes

interface Str {
    function str(Str) -> string
}

type Box = {
    float volume
}
enum Shape { CIRCLE, SQUARE }

function str(Box box) -> string {
    return "Box(" + str(box:volume) + ")"
}

// checked right here: str(Box) exists, so Box can now be passed as a Str
impl Str for Box

function show(Str s) -> string {
    return "<" + s.str() + ">"
}

const Box b = { volume: 2.5 }
io:print_nl(show(b))

// enums and types from other modules can implement interfaces too
function str(Shape s) -> string {
    return match s {
        Shape:CIRCLE -> "circle"
        _ -> "square"
    }
}
impl Str for Shape

function str(shapes:Rect r) -> string {
    return "rect " + str(r.area())
}
impl Str for shapes:Rect
io:print_nl(show(Shape:SQUARE), show(shapes:rect(2, 2)))

// an interface parameter takes part in overloading
function describe(Str s) -> string { return "printable " + s.str() }
function describe(x) -> string { return "something else" }
io:print_nl(describe(b), describe(42))

// generic interfaces; requirements may name parameters and use operators
<T>interface Container {
    function get(Container self, int index) -> T
    function size(Container) -> int
}

type IntList = { int[] items }
function get(IntList l, int i) -> int { return l:items[i] }
function size(IntList l) -> int { return #l:items }
impl Container<int> for IntList

function total(Container<int> c) -> int {
    let sum = 0
    for i in ...c.size() {
        sum += c.get(i)
    }
    return sum
}
io:print_nl(total(IntList{ items: [1, 2, 3] }))

interface Equals {
    function ==(Equals, Equals) -> bool
}
function ==(Box a, Box b) -> bool { return a:volume == b:volume }
impl Equals for Box
function same(Equals a, Equals b) -> bool { return a == b }
io:print_nl(same(b, Box{ volume: 2.5 }), same(b, Box{ volume: 1 }))

// an interface is a value too
io:print_nl(Str, type_of(Str))
