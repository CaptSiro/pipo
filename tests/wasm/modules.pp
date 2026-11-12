import io
import "./geometry" -> g
import "./geometry" -> { vec, add }

enum Op { push(int), pop(), add_top() }

// a tiny stack machine: records, arrays, enums and match working together
function run(Op[] program) -> int[] {
    let int[] stack = []
    for op in program {
        match op {
            push(v) -> { stack.push(v) }
            pop() -> { stack[#stack - 1] = 0 }
            add_top() -> {
                const b = stack[-1]
                const a = stack[-2]
                stack.push(a + b)
            }
        }
    }

    return stack
}

const v = add(vec(1.0, 2.0), g:scale(vec(0.5, 0.5), 4))
io:print_nl(v, g:length_squared(v), g:ORIGIN_NAME)
io:print_nl(run([push(2), push(40), add_top(), push(7)]))

let total = 0.0
for i in 0...5 {
    total += float(i) * 0.5
}

io:print_nl("total", total, int(total * 10.0))
io:print_nl(10 / 0)
io:print_nl("not reached")
