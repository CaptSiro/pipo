# Rebuilds the IntelliJ plugin (editor/pp-intellij) after pp is relinked.
# Run by a POST_BUILD step of the pp target: cmake -DPLUGIN_DIR=... -P build_plugin.cmake
# A failing Gradle build (no JDK, offline, ...) is reported as a warning so it never
# fails the interpreter build.
if (WIN32)
    set(gradlew "${PLUGIN_DIR}/gradlew.bat")
else ()
    set(gradlew "${PLUGIN_DIR}/gradlew")
endif ()

if (NOT EXISTS "${gradlew}")
    message(WARNING "editor plugin not rebuilt: ${gradlew} not found")
    return()
endif ()

# Gradle needs JDK 17+. Use, in order: -DPP_PLUGIN_JAVA_HOME, the JAVA_HOME environment
# variable, or the JBR bundled with the IDE the plugin compiles against (platformPath in
# gradle.properties), so a stray Java 8 on the PATH does not break the build.
set(java_home "${JAVA_HOME_OVERRIDE}")
if (NOT java_home AND DEFINED ENV{JAVA_HOME} AND NOT "$ENV{JAVA_HOME}" STREQUAL "")
    set(java_home "$ENV{JAVA_HOME}")
endif ()
if (NOT java_home AND EXISTS "${PLUGIN_DIR}/gradle.properties")
    file(STRINGS "${PLUGIN_DIR}/gradle.properties" platform REGEX "^platformPath=")
    string(REGEX REPLACE "^platformPath=" "" platform "${platform}")
    if (platform AND EXISTS "${platform}/jbr")
        set(java_home "${platform}/jbr")
    endif ()
endif ()
if (java_home)
    set(ENV{JAVA_HOME} "${java_home}")
endif ()

message(STATUS "Rebuilding the editor plugin (gradlew buildPlugin, JAVA_HOME=${java_home})")
execute_process(
        # --no-daemon: a daemon started here would inherit the build tool's output pipe and keep it
        # open after this step, so ninja (and CLion's build) would wait for it forever
        COMMAND "${gradlew}" buildPlugin --quiet --no-daemon
        WORKING_DIRECTORY "${PLUGIN_DIR}"
        RESULT_VARIABLE code)

if (NOT code EQUAL 0)
    message(WARNING "editor plugin build failed (exit ${code}); pp itself is fine. "
            "Run `gradlew buildPlugin` in ${PLUGIN_DIR} to see why, or configure with -DPP_BUILD_EDITOR_PLUGIN=OFF.")
endif ()
