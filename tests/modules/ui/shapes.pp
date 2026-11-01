type Rect = { float width, float height }

function rect(float width, float height) -> Rect {
    return Rect{ width: width, height: height }
}

function area(Rect r) -> float {
    return r:width * r:height
}

export { Rect, rect, area }
