// interfaces with default implementations, for interface_defaults.pp
// the defaults call operators of types this module never sees

interface Equals {
    function ==(Equals a, Equals b) -> bool
    function !=(Equals a, Equals b) -> bool {
        return !(a == b)
    }
}

interface Comparator {
    function <(Comparator, Comparator) -> bool
    function <=(Comparator, Comparator) -> bool
    function >(Comparator a, Comparator b) -> bool {
        return !(a <= b)
    }
    function >=(Comparator a, Comparator b) -> bool {
        return !(a < b)
    }

    // extending Equals: every Comparator is an Equals too
    impl Equals for Comparator
}

function max(Comparator a, Comparator b) -> Comparator {
    if a >= b {
        return a
    }
    return b
}

function differ(Equals a, Equals b) -> bool {
    return a != b
}

export { Equals, Comparator, max, differ }
