import io

// a Range<T> is iterable only when Iterable<T> is implemented for it
for x in [1]...[3] {
    io:print_nl(x)
}
