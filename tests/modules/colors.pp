// global functions and an enum, for global_functions.pp

enum Color { RED, GREEN, BLUE }

function name(Color c) -> string {
    return match c {
        Color:RED -> "red"
        Color:GREEN -> "green"
        Color:BLUE -> "blue"
    }
}

// usable everywhere once this file is loaded
global function ~(Color c) -> string { return "~" + c.name() }
global <T>function first(T[] items) -> T { return items[0] }

// not exported and not global: dot syntax cannot reach it from outside
function secret(Color c) -> string { return "secret" }

export { Color, name }
