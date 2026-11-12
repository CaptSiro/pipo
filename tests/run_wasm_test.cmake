# Compiles one test script with --target=wasm, runs it with node and compares its stdout with
# <name>.out. If <name>.err exists the program must fail and stderr must contain its text.
get_filename_component(name ${SCRIPT} NAME_WE)
execute_process(
        COMMAND ${PP} --target=wasm -o ${OUT_DIR}/${name} ${SCRIPT}
        OUTPUT_VARIABLE compile_out
        ERROR_VARIABLE compile_err
        RESULT_VARIABLE code)
if (NOT code EQUAL 0)
    message(FATAL_ERROR "pp --target=wasm failed with exit code ${code}:
${compile_err}")
endif ()

execute_process(
        COMMAND ${NODE} ${OUT_DIR}/${name}.mjs
        OUTPUT_VARIABLE actual
        ERROR_VARIABLE errors
        RESULT_VARIABLE code)
string(REPLACE "
" "
" actual "${actual}")
string(REGEX REPLACE "\.out$" ".err" ERRFILE "${EXPECTED}")

if (EXISTS ${ERRFILE})
    file(READ ${ERRFILE} expected_err)
    string(STRIP "${expected_err}" expected_err)
    if (code EQUAL 0)
        message(FATAL_ERROR "expected the program to fail, but it exited with 0")
    endif ()
    string(FIND "${errors}" "${expected_err}" pos)
    if (pos EQUAL -1)
        message(FATAL_ERROR "stderr does not contain:
${expected_err}
--- actual stderr ---
${errors}")
    endif ()
elseif (NOT code EQUAL 0)
    message(FATAL_ERROR "the program failed with exit code ${code}:
${errors}")
endif ()

file(READ ${EXPECTED} expected)
string(REPLACE "
" "
" expected "${expected}")
if (NOT actual STREQUAL expected)
    message(FATAL_ERROR "output mismatch
--- expected ---
${expected}
--- actual ---
${actual}")
endif ()
