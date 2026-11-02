// exports written in front of the declarations, mixed with an export list at the end

export type Celsius = float
export enum Unit { C, F }
export interface Temp {
    function celsius(Temp) -> Celsius
}
export const FREEZING = 0

// a declaration can be exported, the definition fills it in
export function to_f(Celsius) -> float

export <T>function first(T[] xs) -> T {
    return xs[0]
}

function helper(float c) -> float {
    return c * 9 / 5
}

function to_f(Celsius c) -> float {
    return helper(c) + 32
}

function to_c(float f) -> Celsius {
    return (f - 32) * 5 / 9
}

export { to_c -> from_f }
