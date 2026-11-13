// a lambda inside a record literal is typed from the generic record: Todo has no idd
import io

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
io:print_nl(<For each={todos} key={t -> t:idd}>{t -> t:title}</For>)
