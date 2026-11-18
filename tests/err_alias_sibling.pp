// tags of one base type do not convert into each other
type Meters = float
type Seconds = float
function speed(Meters d, Seconds t) -> float {
    return d / t
}

const Meters d = 10.0
const Seconds t = 2.0
speed(t, d)
