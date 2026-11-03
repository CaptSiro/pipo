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
export global native "std_has_range" function has(range, number) -> bool

// ---- std:map<K, V> and std:set<T>: hash containers written in C (src/collections.c)
// Keys and elements must be values that cannot change: bool, number, string, enum values, tuples.
// std:map() / std:set() make empty ones; std:map([tuple("a", 1)]), std:map({ a: 1 }) and
// std:set([1, 2, 2]) copy from a value. Both keep insertion order. A map implements Index
// (at the end of this file), so m[key] and m[key] = value work.
export native type map<K, V>
export native type set<T>

export global native "std_map_find" <K, V>function find(map<K, V> m, K key) -> V?
export global native "std_map_put" <K, V>function put(map<K, V> m, K key, V value) -> void
export global native "std_map_has" <K, V>function has(map<K, V> m, K key) -> bool
export global native "std_map_remove" <K, V>function remove(map<K, V> m, K key) -> V?
export global native "std_map_keys" <K, V>function keys(map<K, V> m) -> K[]
export global native "std_map_values" <K, V>function values(map<K, V> m) -> V[]
export global native "std_map_entries" <K, V>function entries(map<K, V> m) -> tuple<K, V>[]
export global native "std_map_clear" <K, V>function clear(map<K, V> m) -> void

export global <K, V>function in(K key, map<K, V> m) -> bool {
    return m.has(key)
}

// add returns false when the item was already there, remove when it was not
export global native "std_set_add" <T>function add(set<T> s, T item) -> bool
export global native "std_map_has" <T>function has(set<T> s, T item) -> bool
export global native "std_set_remove" <T>function remove(set<T> s, T item) -> bool
export global native "std_map_keys" <T>function values(set<T> s) -> T[]
export global native "std_map_clear" <T>function clear(set<T> s) -> void
export global native "std_set_union" <T>function union(set<T> a, set<T> b) -> set<T>
export global native "std_set_intersection" <T>function intersection(set<T> a, set<T> b) -> set<T>
export global native "std_set_difference" <T>function difference(set<T> a, set<T> b) -> set<T>

export global <T>function in(T item, set<T> s) -> bool {
    return s.has(item)
}

// a | b, a & b, a - b
export global <T>function |(set<T> a, set<T> b) -> set<T> {
    return a.union(b)
}

export global <T>function &(set<T> a, set<T> b) -> set<T> {
    return a.intersection(b)
}

export global <T>function -(set<T> a, set<T> b) -> set<T> {
    return a.difference(b)
}

// ---- Index: what c[key] and c[key] = value call
// c[key] calls [](c, key) and c[key] = value calls []=(c, key, value); `c[key] op= value` uses
// op=(c[key], value) when one exists, else c[key] = c[key] op value. A type that implements
// Index only needs find and put: [] and []= come from the defaults, and work in every module.
export <K, V>interface Index {
    function find(Index collection, K key) -> V?
    function [](Index collection, K key) -> V {
        match collection.find(key) {
            some(value) -> value
            none() -> {
                panic(type_of(collection) + " has no key " + shown(key) + " (find(key) gives an Option instead)")
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

<K, V>impl Index<K, V> for map<K, V>
