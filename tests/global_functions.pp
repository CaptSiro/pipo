import io -> { print }
import "./shapes" -> shapes
import "./modules/colors" -> colors

// `==` is declared `global` in shapes.pp, no import of `==` needed
const r = shapes:rect(2, 3)
const s = shapes:rect(2, 2)
print(r == s, r == shapes:rect(2, 3))
print(==(r, s), r.==(s))
print(r != s)

// built-in overloads of `==` still work next to the global one
print(1 == 1, "a" == "b")

// dot syntax finds functions exported by the module that declared the type
print(r.area(), r.describe())
print(colors:Color:GREEN.name())

// ... merged with functions of the same name in scope
function area(int side) -> int { return side * side }
print(area(4), r.area())

// global operators and generic functions
print(~colors:Color:BLUE, first([7, 8, 9]), [1, 2].first())

// a local declaration shadows an equally good global one
{
    function first(int[] items) -> int { return -1 }
    print(first([7, 8, 9]), first(["a"]))
}
print(first([7, 8, 9]))

// globals are visible inside functions and closures too
function same(shapes:Rect a, shapes:Rect b) -> bool { return a == b }
print(same(r, r), [r, s].map(function(x) { return x == s }))
