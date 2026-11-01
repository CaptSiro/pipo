<T>type Consumer = T -> void
<T>function apply(T value, Consumer<T> consumer) -> void {
    consumer("not a " + str(value))
}
// T is int (from the first argument), so the lambda becomes (int x) -> ...
apply(1, x -> x)
