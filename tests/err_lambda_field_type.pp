type Thunk = () -> string
type Handler = string -> void
type Button = { string label, Handler on_click }
const Button b = { label: "ok", on_click: who -> { } }
b:on_click(42)
