cmake_minimum_required(VERSION 3.16)
project(pp C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

if (NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release)
endif ()

# native modules (io, os, pp, ...) live in src/modules/, one file per module
file(GLOB PP_SOURCES CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/src/*.c ${CMAKE_SOURCE_DIR}/src/modules/*.c)
add_executable(pp ${PP_SOURCES}
        src/modules/std.c
        src/modules/fs.c
        src/modules/math.c)
target_include_directories(pp PRIVATE ${CMAKE_SOURCE_DIR}/src)
# pp.exe always lands in the build directory itself (the $<1:...> stops multi-config
# generators from adding Debug/ or Release/), so lib/ can be copied next to it
# without the copy target depending on pp.
set(PP_OUT_DIR ${CMAKE_BINARY_DIR})
set_target_properties(pp PROPERTIES RUNTIME_OUTPUT_DIRECTORY $<1:${PP_OUT_DIR}>)

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

# packages that ship with the interpreter live in lib/ next to the executable.
# This is its own target that runs on every build (not a POST_BUILD step of pp),
# so changing only files in lib/ still updates the copy, even when pp.exe is up to date.
# The old copy is removed first, so files deleted from lib/ disappear from the build too.
add_custom_target(pp_lib ALL
        COMMAND ${CMAKE_COMMAND} -E remove_directory ${PP_OUT_DIR}/lib
        COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_SOURCE_DIR}/lib ${PP_OUT_DIR}/lib
        COMMENT "Copying lib/ next to pp"
        VERBATIM)
add_dependencies(pp pp_lib) # building just `pp` (e.g. from CLion) refreshes lib/ too

# Every time pp.exe is relinked, rebuild the IntelliJ plugin too (gradlew buildPlugin), so its
# zip in editor/pp-intellij/build/distributions/ matches the interpreter. A POST_BUILD step only
# runs when pp actually changed; a failed plugin build is a warning, not a build error.
option(PP_BUILD_EDITOR_PLUGIN "Rebuild the editor plugin (editor/pp-intellij) whenever pp is rebuilt" ON)
set(PP_PLUGIN_JAVA_HOME "" CACHE PATH "JDK 17+ for the plugin's Gradle build (empty: JAVA_HOME, else the JBR of platformPath)")
if (PP_BUILD_EDITOR_PLUGIN)
    add_custom_command(TARGET pp POST_BUILD
            COMMAND ${CMAKE_COMMAND}
            -DPLUGIN_DIR=${CMAKE_SOURCE_DIR}/editor/pp-intellij
            -DJAVA_HOME_OVERRIDE=${PP_PLUGIN_JAVA_HOME}
            -P ${CMAKE_SOURCE_DIR}/editor/build_plugin.cmake
            COMMENT "Rebuilding the editor plugin"
            VERBATIM)
endif ()

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
