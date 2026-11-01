// an interface and functions that call its requirements, for interface_dispatch.pp
// this module knows nothing about the types that implement View

interface View {
    function view(View) -> string
}

function render(View[] items) -> string {
    let out = ""
    for i in ...#items {
        out += "[" + (i + 1).str() + "]: " + items[i].view() + "\n"
    }
    return out
}

function render_one(View v) -> string {
    return "<" + v.view() + ">"
}

export { View, render, render_one }
