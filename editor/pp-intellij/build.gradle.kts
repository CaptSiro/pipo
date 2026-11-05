import org.jetbrains.intellij.platform.gradle.TestFrameworkType

plugins {
    id("java")
    id("org.jetbrains.intellij.platform") version "2.20.0"
}

group = "com.pipo"
version = "0.2.0"

repositories {
    mavenCentral()
    intellijPlatform {
        defaultRepositories()
    }
}

dependencies {
    intellijPlatform {
        local(providers.gradleProperty("platformPath"))
        testFramework(TestFrameworkType.Platform)
    }
    testImplementation("junit:junit:4.13.2")
    testImplementation("org.opentest4j:opentest4j:1.3.0")
}

tasks.test {
    // the language repository this plugin lives in: its tests/, lib/ and examples/ are parsed by the tests
    systemProperty("pipo.repo", rootDir.resolve("../..").canonicalPath)
}

tasks.withType<JavaCompile> {
    options.release.set(25)
    options.encoding = "UTF-8"
}

intellijPlatform {
    instrumentCode = false
    buildSearchableOptions = false
    pluginConfiguration {
        ideaVersion {
            sinceBuild = "262"
            untilBuild = provider { null }
        }
    }
}

// ./gradlew runClion: starts the local CLion with this plugin, in its own sandbox
intellijPlatformTesting {
    runIde {
        register("runClion") {
            localPath = file(providers.gradleProperty("clionPath").get())
        }
    }
}

// The library modules of the language (../../lib/*.pp: std, fs, math, types) are bundled
// as a fallback, so `import fs` resolves in projects without a lib/ folder or a built pp.
// A real lib/ (the project's, the interpreter's, PP_PATH) always comes first.
val pipoLib = rootDir.resolve("../../lib")
tasks.processResources {
    from(pipoLib) {
        include("*.pp")
        into("pipo/lib")
    }
    inputs.dir(pipoLib).optional()
    doLast {
        val names = pipoLib.listFiles { f -> f.extension == "pp" }?.map { it.nameWithoutExtension }?.sorted() ?: emptyList()
        val dir = destinationDir.resolve("pipo/lib")
        dir.mkdirs()
        dir.resolve("index.txt").writeText(names.joinToString("\n"))
    }
}
