# Runs one test script and compares its stdout with <name>.out.
# If <name>.err exists the script must fail and stderr must contain its text.
execute_process(
        COMMAND ${PP} ${SCRIPT}
        OUTPUT_VARIABLE actual
        ERROR_VARIABLE errors
        RESULT_VARIABLE code)

string(REPLACE "\r\n" "\n" actual "${actual}")
string(REGEX REPLACE "\\.out$" ".err" ERRFILE "${EXPECTED}")

if (EXISTS ${ERRFILE})
    file(READ ${ERRFILE} expected_err)
    string(STRIP "${expected_err}" expected_err)
    if (code EQUAL 0)
        message(FATAL_ERROR "expected the script to fail, but it exited with 0")
    endif ()
    string(FIND "${errors}" "${expected_err}" pos)
    if (pos EQUAL -1)
        message(FATAL_ERROR "stderr does not contain:\n${expected_err}\n--- actual stderr ---\n${errors}")
    endif ()
elseif (NOT code EQUAL 0)
    message(FATAL_ERROR "script failed with exit code ${code}:\n${errors}")
endif ()

if (EXISTS ${EXPECTED})
    file(READ ${EXPECTED} expected)
    string(REPLACE "\r\n" "\n" expected "${expected}")
    if (NOT actual STREQUAL expected)
        message(FATAL_ERROR "output mismatch\n--- expected ---\n${expected}\n--- actual ---\n${actual}")
    endif ()
endif ()
