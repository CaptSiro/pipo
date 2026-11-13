scope _once(Block<void> code) -> void {
    code()
}

generator g() -> int {
    _once {
        emit 1
    }
}
