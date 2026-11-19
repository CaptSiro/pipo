import io
import "./modules/handles/window"

// dot calls on a tagged alias look in the module that declared it, then in those of the tags below it
function run() -> Error<int> {
    const w = window:open(7).lift()
    io:print_nl(w.title(), w.raw())

    const found = window:find(3).unwrap()
    io:print_nl(found.title(), found.raw())

    return success(w.raw())
}

io:print_nl(run().unwrap())
io:print_nl(window:open(0).is_fail())
