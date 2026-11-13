const Generator<int>[] holder = []
generator g() -> int {
    emit holder[0].next().unwrap_or(0)
}

holder.push(g())
holder[0].next()
