interface Str {
    function str(Str) -> string
}
type Box = { float volume }

// the built-in str(any) does not count: Box needs its own str(Box)
impl Str for Box
