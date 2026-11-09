let Callback<void>[] kept = []
scope _keep(Callback<void> code) {
    kept.push(code)
}
function f() {
    _keep {
        return
    }
}
f()
kept[0]()
