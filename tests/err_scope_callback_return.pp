let Callback<void>[] kept = []
scope _keep(Callback<void> code) -> void {
    kept.push(code)
}
function f() -> void {
    _keep {
        return
    }
}
f()
kept[0]()
