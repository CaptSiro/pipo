import io
import "./modules/ui"
import "./modules/ui":shapes
import "./modules/ui":shapes -> { area }
import "./modules/ui":shapes -> geo
import "./modules/ui":widgets
import os

// dot calls find area() in the module that declared Rect (ui:shapes)
const ui:shapes:Rect r = ui:shapes:rect(2, 3)
io:print_nl(r.area())
io:print_nl(ui:shapes:area(r), shapes:area(r), geo:area(r), area(r), r.ui:shapes:area())
io:print_nl(ui:rect(1, 5).area(), type_of(ui:rect(1, 1)))

// multi-level qualified types
function describe(ui:shapes:Rect rect) -> string {
    return str(rect:width) + "x" + str(rect:height)
}
io:print_nl(describe(r), ui:shapes, ui:shapes == shapes)

// a sibling file that the package does not export
io:print_nl(widgets:button("ok"))

// native module
io:print_nl(type_of(os:platform), type_of(os:args()), os)
