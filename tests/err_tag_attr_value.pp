// tag calls (md/TAG_CALLS.md)
type P = { int a }
function f(P p) -> int {
    return p:a
}
const n = 1
const x = <f a=n/>
