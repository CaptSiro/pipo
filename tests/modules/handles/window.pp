// a tag over another module's tag, like glfw:Window = c:ptr
import "./base"

export type Window = base:Handle

export function open(int id) -> Error<Window> {
    if id == 0 {
        return fail("no window")
    }

    return success(id)
}

export function find(int id) -> Window? {
    if id == 0 {
        return none()
    }

    return some(id)
}

export function title(Window w) -> string {
    return "window " + str(w)
}
