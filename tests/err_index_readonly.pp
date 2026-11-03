import io
import std
type Ro = { int[] items }
function [](Ro r, int i) -> int { return r:items[i] }
const Ro r = { items: [1] }
io:print_nl(r[0])
r[0] = 2
