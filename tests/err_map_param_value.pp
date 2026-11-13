import io

// a record converts to a Map<string, V> only when every field is a V
function count(Map<string, int> m) -> int {
    return #m
}

io:print_nl(count({ a: 1, b: "two" }))
