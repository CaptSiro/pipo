import io

interface View {
    function view(View) -> string
}

function render(View[] views, string on_empty) -> void {
    if #options == 0 {
        io:print(on_empty)
        return
    }

    for v in views {
        io:print(v.view())
    }
}

function render(View[] views) -> void {
    render(views, "Collection is empty")
}

function select(View[] options) -> int {
    while true {
        for i in ...#options {
            io:print("[" + (i + 1).str() + "]: " + options[i].view())
        }

        if some(choice) = io:input("Choice: ").parse_int() {
            if 1 <= choice && choice <= #options {
                return choice - 1
            } else {
                io:print(io:nl + "Choice is out of bounds")
            }
        } else {
            io:print(io:nl + "Choice is not a number" + io:nl)
        }
    }
}

export { select, View }