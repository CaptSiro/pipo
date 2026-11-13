// a field for the rest map must be a V of ...Map<string, V>
type D = { string? id, ...Map<string, string> attrs }
function div(D d) -> int {
    return 1
}
const x = div({ id: some("x"), data: 3 })
