type Point = { int x, int y }

enum Color { RED, GREEN }

const SCALE = 10
const secret = "not exported"

function point(int x, int y) -> Point {
    return Point({ x: x, y: y })
}

function add(int a, int b) -> int {
    return a + b
}

function double(Point p) -> Point {
    return point(p:x * 2, p:y * 2)
}

// exports are the last statements of a file
export { Point, point, add, double, Color }
export SCALE -> factor
