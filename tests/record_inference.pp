import io

// generic parameters bound through the fields of a record literal, and lambdas typed from them

<T, K>type Keyer = T -> K
<T>type ItemView = T -> string
<T, K>type ForProps = { T[] each, Keyer<T, K> key }

<T, K>function For(ForProps<T, K> p, ItemView<T>[] children) -> string {
    let out = ""
    for x in p:each {
        out = out + str(p:key(x)) + "=" + children[0](x) + " "
    }

    return out
}

type Todo = { int id, string title }
const todos = [Todo{ id: 1, title: "a" }, Todo{ id: 2, title: "b" }]
io:print_nl(<For each={todos} key={t -> t:id}>{t -> t:title}</For>)
io:print_nl(For({ each: todos, key: t -> t:title }, [t -> str(t:id)]))

<T>type Box = { T value }
<T>function unbox(Box<T> b) -> T {
    return b:value
}

const n = unbox({ value: 41 }) + 1
io:print_nl(n, unbox({ value: "s" }).upper())
