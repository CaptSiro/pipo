// scopes of other modules do not have to be global: mod:_name calls the exported scope
import io
import "./modules/scoped"

function first() -> int {
    scoped:_traced "first" {
        io:print_nl("length " + str(scope))
        return 1
    }

    return 0
}

scoped:_traced("plain") {
    io:print_nl("inside " + str(scope))
}

io:print_nl(str(first()))
const html = scoped:inner:_tagged "b" {
    yield "bold " + scope
}
io:print_nl(html)
scoped:_traced const label = "bound" {
    io:print_nl(label + " " + str(scope))
}
