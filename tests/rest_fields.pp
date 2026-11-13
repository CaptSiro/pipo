import io

// rest fields (md/TAG_CALLS.md): ...Map<string, V> name, the last field of a record type

// ---- a rest field collects the fields a record type does not name
type DivAttrs = { string? id, string? class, ...Map<string, string> attrs }

function div(DivAttrs a) -> string {
    let s = "<div"
    if some(v) = a:id {
        s = s + " id=" + v
    }

    for k, v in a:attrs {
        s = s + " " + k + "=" + v
    }

    return s + ">"
}

io:print_nl(div({ id: some("main"), role: "button" }), div({}))
io:print_nl(<div id={some("x")} data-user-id="42" aria-live="polite" hx-get="/items"/>)
const DivAttrs d = { class: some("c"), title: "t" }
io:print_nl(d:class, d:attrs, #(d:attrs))

// ---- kebab names fill the field a_b when the record has it
type Labels = { string aria_label, ...Map<string, string> rest }
function lab(Labels l) -> string {
    return l:aria_label + " " + str(#(l:rest))
}

io:print_nl(<lab aria-label="Name" data-x="1"/>)

// ---- a typed declaration converts too; the rest map holds the extra fields
type D = { int a, ...Map<string, int> more }
const D dd = { a: 1, b: 2, c: 3 }
io:print_nl(dd:a, dd:more)
