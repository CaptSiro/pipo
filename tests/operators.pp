import io
// the built-in binary operators: every overload registered in builtins_register

// ---- + : (number, number), (string, string)
io:print_nl(2 + 3, -2 + 3, 2 + 0.5, 0.5 + 2, 1.5 + 1.5, "ab" + "cd", "" + "")
// ---- - : (number, number)
io:print_nl(5 - 3, 3 - 5, 5 - 0.5, 0.5 - 5, 2.5 - 0.5)
// ---- * : (number, number), (string, int)
io:print_nl(4 * 3, -4 * 3, 4 * 0.5, 0.5 * 4, 1.5 * 2.0, "ab" * 3, "ab" * 0)
// ---- / : (number, number); int / int truncates toward zero, anything with a float is float
io:print_nl(7 / 2, -7 / 2, 7 / -2, 6 / 3, 7.0 / 2, 7 / 2.0, 1 / 4.0)
// ---- % : (number, number); the result takes the sign of the divisor
io:print_nl(7 % 3, -7 % 3, 7 % -3, -7 % -3, 6 % 3, 7.5 % 2, -7.5 % 2, 5 % -1)
// ---- ** : (number, number); a negative or float exponent gives a float
io:print_nl(2 ** 10, 3 ** 0, 0 ** 0, (-2) ** 3, 2 ** -1, 4 ** 0.5, 2.0 ** 3)

// int arithmetic wraps on overflow
const int max = 9223372036854775807
io:print_nl(max + 1 == -max - 1, max * 2, 2 ** 64)

// ---- precedence: ** over * / % over + - over ... over comparisons over == != over && over ||
io:print_nl(2 + 3 * 4, (2 + 3) * 4, 2 * 3 ** 2, 10 - 4 - 3, 100 / 10 / 5, 2 + 6 % 4, 1 + 2 < 4, 1 < 2 == 3 < 4)

// ---- comparisons: (any, any) -> bool
io:print_nl(1 < 2, 2 < 1, 1 < 1, 1 <= 1, 2 <= 1, 2 > 1, 1 > 2, 1 >= 1, 1 >= 2)
io:print_nl(1 < 1.5, 1.5 < 1, 2.0 <= 2, 2 >= 2.0)
io:print_nl("a" < "b", "b" < "a", "ab" < "abc", "abc" <= "abc", "b" > "abc", "B" < "a")

// ---- == and != : (any, any) -> bool, by value
io:print_nl(1 == 1, 1 == 2, 1 != 2, 1 == 1.0, "a" == "a", "a" != "b", 1 == "1")
io:print_nl([1, 2] == [1, 2], [1, 2] == [2, 1], [1] != [1, 2], { a: 1 } == { a: 1 }, true == true, true != false)

// ---- && and || short-circuit
function loud(bool v) -> bool {
    io:print("[", v, "]")
    return v
}
io:print_nl(true && true, true && false, false && true, false || false, false || true, true || false)
io:print_nl(loud(false) && loud(true), loud(true) || loud(false), loud(true) && loud(false))

// ---- ... and ...= : (int, int) -> range
io:print_nl(0...3, 0...=3, 3...0, 3...=0)
for i in 2...5 { io:print(i, "") }
io:print_nl()
for i in 2...=5 { io:print(i, "") }
io:print_nl()

// ---- compound assignment: x op= y is x = x op y for every operator above
let int i = 10
i += 5
io:print(i, "")
i -= 3
io:print(i, "")
i *= 2
io:print(i, "")
i /= 4
io:print(i, "")
i %= 4
io:print(i, "")
i **= 5
io:print_nl(i)
let s = "ab"
s += "c"
s *= 2
io:print_nl(s)
let float f = 1.5
f += 1
f *= 2
f /= 4
io:print_nl(f)

// ---- operators are functions: called by name, passed as values, extended by overloads
io:print_nl(+(1, 2), -(5, 2), *("x", 3), /(9, 3), %(9, 4), **(2, 3), <(1, 2), ==(1, 1))
io:print_nl(reduce([1, 2, 3, 4], *, 1), reduce(["a", "b", "c"], +, ""))
type Vec = { int x, int y }
function +(Vec a, Vec b) -> Vec { return { x: a:x + b:x, y: a:y + b:y } }
function *(Vec a, int k) -> Vec { return { x: a:x * k, y: a:y * k } }
let Vec v = { x: 1, y: 2 }
v += { x: 10, y: 20 }
v *= 3
io:print_nl(v:x, v:y, 1 + 2, "a" + "b")
