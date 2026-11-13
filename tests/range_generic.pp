import io
import std

// ---- numbers: ... and ...= with floats, the end as written, printing as written
io:print_nl(0...5, 0...=5, (0...=5):end, (0...=5):inclusive, 5...=1, (5...=1):end)
io:print_nl(0.5...=2.5, (0.5...=2.5):end, #(0...=5), type_of(0.5...2), type_of(1...3))
for x in 0.5...3 {
    io:print(x, " ")
}

io:print_nl()

// ---- Range<string>: code points, bounds compared as strings
const letters = "a"...="e"
io:print_nl(letters, type_of(letters), letters:start, letters:end, letters:inclusive)
for ch in letters {
    io:print(ch)
}

io:print_nl()
for ch in "e"..."a" {
    io:print(ch)
}

io:print_nl()
io:print_nl("c" in letters, "x" in letters, "cc" in letters, "e" in "a"..."e", "b" in "e"..."a")
io:print_nl(letters == "a"...="e", letters == "a"..."e", ("a"...="c").to_array())

function initials(Range<string> r) -> string {
    return join(r.to_array(), "")
}

io:print_nl(initials("A"...="F"))

// ---- Range<Day>: a user type gets `in` from Comparator and `for` from Iterable
type Day = { int n }

function ==(Day a, Day b) -> bool {
    return a:n == b:n
}

function <(Day a, Day b) -> bool {
    return a:n < b:n
}

function <=(Day a, Day b) -> bool {
    return a:n <= b:n
}

impl std:Comparator for Day

generator iter(Range<Day> r) -> Day {
    for i in r:start:n...r:end:n {
        emit Day{ n: i }
    }

    if r:inclusive {
        emit r:end
    }
}

impl Iterable<Day> for Range<Day>

const week = Day{ n: 1 }...=Day{ n: 3 }
for d in week {
    io:print(d:n, " ")
}

io:print_nl()
io:print_nl(Day{ n: 2 } in week, Day{ n: 9 } in week, week:end:n, type_of(week))
const Range<Day> span = Day{ n: 5 }...Day{ n: 7 }
io:print_nl(#(span.to_array()))
