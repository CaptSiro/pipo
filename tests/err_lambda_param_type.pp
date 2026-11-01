import io
type ToInt = float -> int
const ToInt cb = f -> int(f)
io:print(cb(3.7), cb(2))
cb("12")
