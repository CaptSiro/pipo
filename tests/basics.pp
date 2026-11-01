import io
// basics
io:print("hello", 1 + 2 * 3, 10 / 3, 10.0 / 4, 7 % 3, -7 % 3, 2 ** 10)
let x = 5
x += 2
io:print(x, -x, #"hello", #[1,2,3])
const name
io:print(name, defined("name"), defined("nope"))
for i in ...5 { io:write(i, "") }
io:print()
for let i in 10...=1 { io:write(i, "") }
io:print()
let arr = [3, 1, 2]
for i, v in arr { io:write(i, ":", v, " ") }
io:print()
io:print(sort(arr), arr.reverse(), arr.map(function(v) { return v * 10 }))
io:print(reduce(arr, +, 0))
function fib(int n) -> int {
    if n < 2 { return n }
    return fib(n - 1) + fib(n - 2)
}
io:print(fib(20))
<T>function +(T[] a, T[] b) -> T[] { return a.concat(b) }
<T>function *(T[] array, int count) -> T[] { return array.multiply(count) }
precedence * over +
io:print(['b'] + ['a'] * 5)
io:print(+([], []), [].+([1]))
io:print(success 5, fail "bad", some(3), none())
const v = match 5 {
    10 -> 1
    5 -> { "five" }
    _ -> 0
}
io:print(v)
type Person = { string name, number age }
const Person john = { name: "John Pork", age: 22 }
const Person[] people = [john]
for p in people { io:print(p) }
function greet(Person p) -> string { return "Hi " + p:name }
io:print(john.greet())
if some(y) = parse_int("42") { io:print("parsed", y) } else { io:print("no") }
if some(y) = parse_int("x") { io:print("parsed", y) } else { io:print("no") }
io:print(unwrap(some(1)), unwrap_or(none(), 7), unwrap(success(9)), is_fail(fail("x")))
enum Properties { NAME, VALUE, DATASET }
enum string Messages { NOTICE = "Notice", WARNING = "Warning", ERROR = "Error" }
io:print(Properties:VALUE, enum_value(Properties:DATASET), enum_value(Messages:WARNING))
match Messages:ERROR {
    Messages:NOTICE -> io:print("notice")
    Messages:ERROR -> io:print("error!")
    _ -> io:print("?")
}
let i = 0
while true { i += 1; if i > 3 { break } }
io:print(i)
io:print(1.0, 0.1 + 0.2, 1e100, 3.5)
