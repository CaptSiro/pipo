// scopes exported by a module are called as scoped:_name, and through submodules as scoped:inner:_name
import io
export import "./scoped/inner"

export scope _traced(Block<int> code, string label) -> void {
    io:print_nl("enter " + label)
    defer {
        io:print_nl("leave " + label)
    }

    code(#label)
}

scope _hidden(Block code) -> void {
    code()
}
