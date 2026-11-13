import io

// slice takes consecutive indices: a range with another step is an error
io:print_nl([1, 2, 3, 4].slice(range(0, 4, 2)))
