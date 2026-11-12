// a module for modules.pp: types, functions and a global the importer uses
export type Vec = { float x, float y }

export const ORIGIN_NAME = "origin"

export function vec(float x, float y) -> Vec {
    return { x: x, y: y }
}

export function add(Vec a, Vec b) -> Vec {
    return vec(a:x + b:x, a:y + b:y)
}

export function scale(Vec v, float k) -> Vec {
    return vec(v:x * k, v:y * k)
}

export function length_squared(Vec v) -> float {
    return v:x * v:x + v:y * v:y
}
