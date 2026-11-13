// tag calls (md/TAG_CALLS.md)
type Person = { int age, string name }
type Tree = { Person value, Tree[] children }
function parent(Person person, Tree[] children) -> Tree {
    return Tree{ value: person, children }
}
const p = <parent age={32} name="Tom"/>
