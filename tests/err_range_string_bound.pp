import io

// a range of strings goes through code points: each bound must be one
for s in "aa"..."c" {
    io:print_nl(s)
}
