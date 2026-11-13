import io
import "./modules/markup" -> h

// ---- tag calls (md/TAG_CALLS.md): <f a={x}>children</f> is f({ a: x }, [children])
type Person = { int age, string name }
type Tree = { Person value, Tree[] children }

function parent(Person person, Tree[] children) -> Tree {
    return Tree{ value: person, children }
}

function child(Person person) -> Tree {
    return Tree{ value: person, children: [] }
}

const family = <parent age={32} name="Tom">
    <child age={5} name="James"/>
    // comments may sit between children
    <child age={7} name="Ann"/>
</parent>

io:print_nl(family:value:name, #(family:children), family:children[1]:value:name)

// ---- children only: f([...]); an empty body is []
function row(string[] cells) -> string {
    return "|" + join(cells, "|") + "|"
}

io:print_nl(<row>"a" "b" {"c" + "d"}</row>, <row></row>)

// ---- T? attributes may be left out (they are none()); <f/> is f() or f({}), <f>..</f> f({}, [..])
type Opts = { int? size, string? label, bool? data_x }

function box(Opts o) -> string {
    return "box " + str(o:size.unwrap_or(0)) + " " + o:label.unwrap_or("-") + " " + str(o:data_x.unwrap_or(false))
}

function box(Opts o, string[] inner) -> string {
    return box(o) + " [" + join(inner, ",") + "]"
}

io:print_nl(<box/>, <box size={some(3)}/>, <box>"x"</box>)
io:print_nl(<box label={some("L")} size={some(1 + 1)}>"y" "z"</box>, <box></box>, <box data-x={some(true)}/>)

// ---- f(): a function without parameters; a tag is an operand, `a <b` stays a comparison
function hello() -> string {
    return "hello"
}

const a = 1
const b = 2
io:print_nl(<hello/>, a <b, a < b, <hello/> + "!")

// ---- a record type: <Person .../> is Person({ ... })
const tom = <Person age={40} name="Tom"/>
io:print_nl(tom:name, tom:age)

// ---- a module path, kebab-case attributes (aria-label is the field aria_label)
const page = <h:div id={some("main")} class={some("card")}>
    <h:p>"Hello, " {tom:name} "!"</h:p>
    <h:br/>
    <h:input aria-label={some("Name")}/>
    <h:div>"nested"</h:div>
</h:div>
io:print_nl(page)

// ---- text is kept exactly: no whitespace rule
io:print_nl(<h:p>"  two  spaces
and a newline"</h:p>)

// ---- generic declarations still parse next to tags, and generic functions take tags
<T>function first(T[] items) -> T {
    return items[0]
}

io:print_nl(<first>"a" "b"</first>, <first>{1} {2}</first>)
