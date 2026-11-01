// not exported by ui.pp: `import "./modules/ui":widgets` finds this file next to it
import "./shapes"

function button(string label) -> string {
    return "[" + label + "] " + str(shapes:rect(4, 1).area())
}

export button
