// tag calls (md/TAG_CALLS.md)
type Person = { int age, string name }
type Tree = { Person value, Tree[] children }
function child(Person person) -> Tree {
    return Tree{ value: person, children: [] }
}
const c = <child age={5} name="James">
</child>
