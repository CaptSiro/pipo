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
// has(r, x): is x one of the elements `for v in r` visits
export global native "std_range_has" function has(range, number) -> bool

// ---- std:Map<K, V> and std:Set<T>: hash containers written in C (src/collections.c)
// Keys and elements must be values that cannot change: bool, number, string, enum values, tuples.
// The types are not callable; std:map() / std:set() make empty ones, and std:map([tuple("a", 1)]),
// std:map({ a: 1 }), std:map(other), std:set([1, 2, 2]), std:set(0...5) and std:set(other) copy
// from a value. Both keep insertion order. A Map implements Index (at the end of this file), so
// m[key] and m[key] = value work.
export native type Map<K, V>
export native type Set<T>

export native "std_map_new" function map() -> Map
export native "std_map_new" function map(tuple<any, any>[] entries) -> Map
export native "std_map_new" function map(object fields) -> Map
export native "std_map_new" function map(Map other) -> Map

export native "std_set_new" function set() -> Set
export native "std_set_new" function set(any[] items) -> Set
export native "std_set_new" function set(range items) -> Set
export native "std_set_new" function set(Set other) -> Set

export global native "std_map_get" <K, V>function get(Map<K, V> m, K key) -> V?
export global native "std_map_put" <K, V>function put(Map<K, V> m, K key, V value) -> void
export global native "std_map_has" <K, V>function has(Map<K, V> m, K key) -> bool
export global native "std_map_remove" <K, V>function remove(Map<K, V> m, K key) -> V?
export global native "std_map_keys" <K, V>function keys(Map<K, V> m) -> K[]
export global native "std_map_values" <K, V>function values(Map<K, V> m) -> V[]
export global native "std_map_entries" <K, V>function entries(Map<K, V> m) -> tuple<K, V>[]
export global native "std_map_clear" <K, V>function clear(Map<K, V> m) -> void

export global <K, V>function in(K key, Map<K, V> m) -> bool {
    return m.has(key)
}

// add returns false when the item was already there, remove when it was not
export global native "std_set_add" <T>function add(Set<T> s, T item) -> bool
export global native "std_map_has" <T>function has(Set<T> s, T item) -> bool
export global native "std_set_remove" <T>function remove(Set<T> s, T item) -> bool
export global native "std_map_keys" <T>function values(Set<T> s) -> T[]
export global native "std_map_clear" <T>function clear(Set<T> s) -> void
export global native "std_set_union" <T>function union(Set<T> a, Set<T> b) -> Set<T>
export global native "std_set_intersection" <T>function intersection(Set<T> a, Set<T> b) -> Set<T>
export global native "std_set_difference" <T>function difference(Set<T> a, Set<T> b) -> Set<T>

export global <T>function in(T item, Set<T> s) -> bool {
    return s.has(item)
}

// a | b, a & b, a - b
export global <T>function |(Set<T> a, Set<T> b) -> Set<T> {
    return a.union(b)
}

export global <T>function &(Set<T> a, Set<T> b) -> Set<T> {
    return a.intersection(b)
}

export global <T>function -(Set<T> a, Set<T> b) -> Set<T> {
    return a.difference(b)
}

// ---- Index: what c[key] and c[key] = value call
// c[key] calls [](c, key) and c[key] = value calls []=(c, key, value); `c[key] op= value` uses
// op=(c[key], value) when one exists, else c[key] = c[key] op value. A type that implements
// Index only needs get and put: [] and []= come from the defaults, and work in every module.
export <K, V>interface Index {
    function get(Index collection, K key) -> V?
    function [](Index collection, K key) -> V {
        return match collection.get(key) {
            some(value) -> value
            none() -> {
                panic(type_of(collection) + " has no key " + shown(key))
            }
        }
    }

    function put(Index collection, K key, V value) -> void
    function []=(Index collection, K key, V value) -> void {
        collection.put(key, value)
    }
}

// a key as it would be written: "b" for a string, tuple(1, 2) for a tuple
function shown(any value) -> string {
    return if type_of(value) == "string" {
        str([value]).slice(1, -1)
    } else {
        str(value)
    }
}

<K, V>impl Index<K, V> for Map<K, V>
