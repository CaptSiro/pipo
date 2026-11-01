// for pp_module.pp: pp:* describe the file a call is written in, not the entry script
import os
import pp

function here() -> string { return pp:dir() }
function this_file() -> string { return pp:file() }
function sibling(string name) -> string { return os:join_pp(name) }

export { here, this_file, sibling }
