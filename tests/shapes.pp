// a small module used by proposal.pp

type Rect = {
    number width,
    number height,
}

function rect(number width, number height) -> Rect {
    return Rect({ width: width, height: height })
}

// "methods" are functions whose first parameter is the datatype
function area(Rect r) -> number {
    return r:width * r:height
}

function describe(Rect r) -> string {
    return "Rect " + str(r:width) + "x" + str(r:height)
}

// operators are functions too. `global` makes == usable in every file once
// this module is loaded, without importing it
global function ==(Rect a, Rect b) -> bool {
    return a:width == b:width && a:height == b:height
}

const UNIT = rect(1, 1)

export { Rect, rect, area, describe, ==, UNIT }
