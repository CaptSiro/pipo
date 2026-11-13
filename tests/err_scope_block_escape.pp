let Block<void>[] kept = []
scope _keep(Block<void> code) -> void {
    kept.push(code)
}
_keep { }
kept[0]()
