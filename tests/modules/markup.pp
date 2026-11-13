// a tiny markup library for the tag call tests: elements build HTML strings
type Attrs = { string? id, string? class, string? aria_label }

function open(string tag, Attrs a) -> string {
    let s = "<" + tag
    if some(v) = a:id {
        s = s + " id=\"" + v + "\""
    }

    if some(v) = a:class {
        s = s + " class=\"" + v + "\""
    }

    if some(v) = a:aria_label {
        s = s + " aria-label=\"" + v + "\""
    }

    return s + ">"
}

function div(Attrs a, string[] children) -> string {
    return open("div", a) + join(children, "") + "</div>"
}

function div(string[] children) -> string {
    return div({}, children)
}

function p(string[] children) -> string {
    return "<p>" + join(children, "") + "</p>"
}

// a void element: no children
function br() -> string {
    return "<br>"
}

function input(Attrs a) -> string {
    return open("input", a)
}

export { Attrs, div, p, br, input }
