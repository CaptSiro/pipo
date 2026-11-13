// tag calls (md/TAG_CALLS.md)
type P = { int a }
function f(P p) -> int {
    return p:a
}
const x = <f a={1} a={2}/>
