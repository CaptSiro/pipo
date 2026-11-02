import io
import "./modules/ordering" -> ord

type Money = { int cents }
enum Level { LOW, HIGH }
type Version = { int major }

function ==(Money a, Money b) -> bool { return a:cents == b:cents }
function <(Money a, Money b) -> bool { return a:cents < b:cents }
function <=(Money a, Money b) -> bool { return a:cents <= b:cents }

// Comparator extends Equals, so this implements both; != > >= come from the defaults
impl ord:Comparator for Money

const Money a = { cents: 100 }
const Money b = { cents: 250 }
io:print(a != b, a != a, a > b, b > a, a >= a, b >= a)
io:print(ord:max(a, b), ord:max(b, a):cents)

// a Comparator is accepted where an Equals is expected
function same(ord:Equals x, ord:Equals y) -> bool { return x == y }
function check(ord:Comparator x, ord:Comparator y) -> bool { return same(x, y) }
io:print(check(a, Money{ cents: 100 }), ord:differ(a, b))

// a function of the type wins over the default
function ==(Level x, Level y) -> bool { return x.enum_value() == y.enum_value() }
function !=(Level x, Level y) -> bool { return true }
impl ord:Equals for Level
io:print(Level:LOW != Level:LOW, ord:differ(Level:LOW, Level:LOW))

// implementing the parent first is fine, Comparator reuses it
function ==(Version a, Version b) -> bool { return a:major == b:major }
impl ord:Equals for Version
function <(Version a, Version b) -> bool { return a:major < b:major }
function <=(Version a, Version b) -> bool { return a:major <= b:major }
impl ord:Comparator for Version
io:print(Version{ major: 2 } > Version{ major: 1 }, Version{ major: 2 } != Version{ major: 2 })

// defaults can call other requirements by dot syntax and use generics of the interface
<T>interface Bag {
    function items(Bag) -> T[]
    function first(Bag bag) -> T {
        return bag.items()[0]
    }
    function count(Bag bag) -> int {
        return #bag.items()
    }
}
type Words = { string[] list }
function items(Words w) -> string[] { return w:list }
impl Bag<string> for Words
const Words w = { list: ["alpha", "beta"] }
io:print(w.first(), w.count())
