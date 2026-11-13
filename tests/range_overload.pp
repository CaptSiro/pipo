// ... and ...= are the only names with '.' that can be declared: they overload like any operator
import io

type Day = { int n }

function ...(Day a, Day b) -> Day[] {
    const Day[] out = []
    for i in a:n...b:n { out.push(Day{ n: i }) }
    return out
}

function ...=(Day a, Day b) -> Day[] {
    return a...Day{ n: b:n + 1 }
}

io:print_nl(Day{ n: 1 }...Day{ n: 3 }, Day{ n: 1 }...=Day{ n: 2 })
io:print_nl(#(0...5), 1...=3)
for i in ...3 { io:print(i) }
io:print_nl()
