import io

// int ranges are unchanged
io:print_nl(0...5, 5...=1, range(0, 10, 3), #range(0, 10, 3))
for i in range(10, 0, -4) { io:print(i, " ") }
io:print_nl()

// number ranges: any float makes the whole range float
const quarters = range(0, 1, 0.25)
io:print_nl(quarters, #quarters)
for i, v in quarters { io:print(i, ":", v, " ") }
io:print_nl()
for v in range(1.0, 0.0, -0.3) { io:print(v, " ") }
io:print_nl()
io:print_nl(#range(0, 1, 0.1), #range(0.0, 0.0, 1), #range(1, 2, -0.5))

// fields: start, end, step
const range r = range(2, 20, 3)
io:print_nl(r:start, r:end, r:step, quarters:step, (5...=1):end)
io:print_nl(range(0, 1, 0.5) == range(0.0, 1.0, 0.5), range(0, 4, 2) == 0...4)
