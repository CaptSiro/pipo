// a type that implements std:Index; importers index it without importing [] or []=
import std

export type Board = { int width, string[] cells }

export function board(int width, int height) -> Board {
    const cells = [].repeat(0)
    for i in ...(width * height) { cells.push(".") }
    return Board({ width: width, cells: cells })
}

// only find and put are written: [] and []= are the interface's defaults
function find(Board b, tuple<int, int> at) -> string? {
    const i = at[1] * b:width + at[0]
    if i < 0 || i >= #b:cells { return none() }
    return some(b:cells[i])
}

function put(Board b, tuple<int, int> at, string value) -> void {
    b:cells[at[1] * b:width + at[0]] = value
}

impl std:Index<tuple<int, int>, string> for Board

export function rows(Board b) -> string[] {
    const out = [].repeat(0)
    for y in ...(#b:cells / b:width) { out.push(b:cells.slice(y * b:width, (y + 1) * b:width).join("")) }
    return out
}
