import io -> { print_nl }
import "./shapes" -> shapes
import "./modules/colors" -> colors

// `==` is declared `global` in shapes.pp, no import of `==` needed
const r = shapes:rect(2, 3)
const s = shapes:rect(2, 2)
print_nl(r == s, r == shapes:rect(2, 3))
print_nl(==(r, s), r.==(s))
print_nl(r != s)

// built-in overloads of `==` still work next to the global one
print_nl(1 == 1, "a" == "b")

// dot syntax finds functions exported by the module that declared the type
print_nl(r.area(), r.describe())
print_nl(colors:Color:GREEN.name())

// ... merged with functions of the same name in scope
function area(int side) -> int { return side * side }
print_nl(area(4), r.area())

// global operators and generic functions
print_nl(~colors:Color:BLUE, first([7, 8, 9]), [1, 2].first())

// a local declaration shadows an equally good global one
{
    function first(int[] items) -> int { return -1 }
    print_nl(first([7, 8, 9]), first(["a"]))
}
print_nl(first([7, 8, 9]))

// globals are visible inside functions and closures too
function same(shapes:Rect a, shapes:Rect b) -> bool { return a == b }
print_nl(same(r, r), [r, s].map(function(x) { return x == s }))
