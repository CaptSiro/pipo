import math -> { sqrt }

type Vec = { float x, float y }
function vec(float x, float y) -> Vec { return Vec({ x: x, y: y }) }
function +(Vec a, Vec b) -> Vec { return vec(a:x + b:x, a:y + b:y) }
function length(Vec v) -> float { return sqrt(v:x * v:x + v:y * v:y) }
const ORIGIN = vec(0, 0)

export { Vec, vec, +, length, ORIGIN }
