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

export <T>function array(int capacity) -> T[] {
    return [].capacity(capacity);
}

// x in r: is x between the range's bounds (0...5 is [0, 5), 5...=1 is [1, 5]); the step is ignored
export global native "std_in_range" function in(number, Range) -> bool
// a in b: are a's bounds within b's, ends included or not: (1...3) in (0...3) and (0...=2) in (0...3) are
// true, (0...=3) in (0...3) is not. The direction and the step do not matter; an empty range is in every range.
export global function in(Range inner, Range outer) -> bool {
    if inner.is_empty() {
        return true
    }

    // on a shared bound, an included end of inner needs an included end of outer
    const low_a = inner.low()
    const low_b = outer.low()
    const low_in = low_a > low_b || low_a == low_b && (outer.low_included() || !inner.low_included())

    const high_a = inner.high()
    const high_b = outer.high()
    const high_in = high_a < high_b || high_a == high_b && (outer.high_included() || !inner.high_included())

    return low_in && high_in
}

// ---- a range's bounds as an interval, lowest first: 0...5 is [0, 5), 5...0 is (0, 5], 0...=5 and 5...=0 are [0, 5]
export function low(Range r) -> number {
    if r:step > 0 {
        return r:start
    }

    return r:end
}

export function high(Range r) -> number {
    if r:step < 0 {
        return r:start
    }

    return r:end
}

export function low_included(Range r) -> bool {
    return r:step > 0 || r:inclusive
}

export function high_included(Range r) -> bool {
    return r:step < 0 || r:inclusive
}

// 0...0 and 5...5 have no elements; 0...=0 has one
export function is_empty(Range r) -> bool {
    if r.low() == r.high() {
        return !(r.low_included() && r.high_included())
    }

    return r.low() > r.high()
}

// has(r, x): is x one of the elements `for v in r` visits
export global native "std_range_has" function has(Range, number) -> bool



// is_digit(c): is c one of the characters 0 to 9 ("12", "" and "5x" are not)
export function is_digit(string c) -> bool {
    return #c == 1 && c in "0"...="9"
}
