// the pp module: location of the current script, like PHP's __FILE__ / __DIR__ / __LINE__
import io
import os
import pp
import "./modules/location"

const sep = if os:platform == "windows" { "\\" } else { "/" }

// file() is dir() joined with the file name, and both are absolute
io:print(pp:file() == pp:dir() + sep + "pp_module.pp")
io:print(pp:file().ends_with(sep + "tests" + sep + "pp_module.pp"))
io:print(pp:line())

// inside an imported module they describe that module, wherever it is called from
io:print(location:here() == os:join(pp:dir(), "modules"))
io:print(location:this_file() == os:join_pp("modules", "location.pp"))
io:print(location:sibling("views.pp") == os:join_pp("modules", "views.pp"))

// join_pp works regardless of the working directory the script was started from
io:print(io:read_file(os:join_pp("modules", "location.pp")).is_success())

// os:join: one separator between parts, empty parts skipped, an absolute part starts over
io:print(os:join("a", "b", "c") == "a" + sep + "b" + sep + "c")
io:print(os:join("a/", "b") == "a/b", os:join("a/", "/b") == "/b",os:join("a", "", "b") == "a" + sep + "b")
io:print(os:join("a", "/root", "x") == "/root" + sep + "x")
io:print(os:join() == "")
