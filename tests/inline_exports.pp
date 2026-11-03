import io
import "./modules/inline_exports" -> t

io:print_nl(t:to_f(100), t:from_f(212), t:FREEZING)
io:print_nl(t:first([t:Unit:F, t:Unit:C]), t:Unit:C)

const t:Celsius room = 21.5
io:print_nl(t:to_f(room))

type Probe = { float c }
function celsius(Probe p) -> t:Celsius { return p:c }
impl t:Temp for Probe
io:print_nl(Probe{ c: 4 }.celsius())
