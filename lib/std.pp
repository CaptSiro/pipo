export interface Str {
    function str(Str) -> string
}

export interface Equals {
    function ==(Equals, Equals) -> bool
    function !=(Equals a, Equals b) -> bool {
        return !(a == b)
    }
}

export interface Comparator {
    function <(Comparator, Comparator) -> bool
    function <=(Comparator, Comparator) -> bool
    function >(Comparator a, Comparator b) -> bool {
        return !(a <= b)
    }
    function >=(Comparator a, Comparator b) -> bool {
        return !(a < b)
    }

    impl Equals for Comparator
}

export <T>type Consumer = T -> void
export <T>type Predicate = T -> bool
export <T, R>type Producer = T -> R

export global <T>function in(T item, T[] array) -> bool {
    return array.contains(item)
}

export global function in(string needle, string haystack) -> bool {
    return haystack.contains(needle)
}

// x in r: is x between the range's bounds (0...5 is [0, 5), 5...=1 is [1, 5]); the step is ignored
export global native "std_in_range" function in(number, range) -> bool
// has(x, r): is x one of the elements `for v in r` visits
export global native "std_has_range" function has(number, range) -> bool
