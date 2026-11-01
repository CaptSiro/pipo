cmake_minimum_required(VERSION 3.16)
project(pp C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

if (NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release)
endif ()

file(GLOB PP_SOURCES CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/src/*.c)
add_executable(pp ${PP_SOURCES})

if (MSVC)
    target_compile_definitions(pp PRIVATE _CRT_SECURE_NO_WARNINGS)
    target_compile_options(pp PRIVATE /W3)
    # deep recursion in the tree-walking interpreter needs a bigger stack
    target_link_options(pp PRIVATE /STACK:67108864)
else ()
    target_compile_options(pp PRIVATE -Wall -Wextra -Wno-unused-parameter)
    if (WIN32)
        # C99 compliant printf family on MinGW
        target_compile_definitions(pp PRIVATE __USE_MINGW_ANSI_STDIO=1)
        target_link_options(pp PRIVATE -Wl,--stack,67108864 -static)
    endif ()
    target_link_libraries(pp m)
endif ()

# packages that ship with the interpreter live in lib/ next to the executable
add_custom_command(TARGET pp POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_SOURCE_DIR}/lib $<TARGET_FILE_DIR:pp>/lib)

enable_testing()
file(GLOB PP_TESTS CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/tests/*.pp)
foreach (test_file ${PP_TESTS})
    get_filename_component(test_name ${test_file} NAME_WE)
    get_filename_component(test_dir ${test_file} DIRECTORY)
    add_test(NAME ${test_name}
            COMMAND ${CMAKE_COMMAND}
            -DPP=$<TARGET_FILE:pp>
            -DSCRIPT=${test_file}
            -DEXPECTED=${test_dir}/${test_name}.out
            -P ${CMAKE_SOURCE_DIR}/tests/run_test.cmake)
endforeach ()
