import io, os, fs, "./modules/mathx", types -> t, math -> { pi }, math -> m, "./modules/ui":shapes
import "./modules/ui":shapes -> { rect, area },
    std

// several modules in one import, each written as it would be on a line of its own
io:print_nl(mathx:add(1, 2), pi == m:pi, #t:int8array(3), os:platform != "", fs:exists(os:join_pp("multi_import.pp")))
io:print_nl(shapes:area(shapes:rect(2, 3)), area(rect(1, 4)), std:is_digit("7"))
