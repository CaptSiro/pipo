import io
function never_called() -> int {
    return "not an int"
}
io:print_nl("this line never runs: the program is rejected before it starts")
