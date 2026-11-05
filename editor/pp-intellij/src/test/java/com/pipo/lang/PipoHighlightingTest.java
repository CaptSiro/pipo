package com.pipo.lang;

import com.intellij.codeInsight.daemon.impl.HighlightInfo;
import com.intellij.lang.annotation.HighlightSeverity;
import com.intellij.openapi.util.text.StringUtil;
import com.intellij.testFramework.fixtures.BasePlatformTestCase;
import com.pipo.lang.highlight.PipoUnresolvedReferenceInspection;

import java.io.File;
import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.ArrayList;
import java.util.List;
import java.util.concurrent.TimeUnit;
import java.util.stream.Stream;

/**
 * Highlights every .pp file of the repository (tests/, lib/, examples/) in a
 * project that contains them, so imports resolve as they do for the interpreter.
 * <ul>
 *   <li>A file the interpreter rejects with a syntax error must get an error.</li>
 *   <li>Every other file must get no error at all.</li>
 *   <li>Unresolved names must not appear in tests/ and lib/ (the err_ tests excepted);
 *       those in examples/ are only listed.</li>
 * </ul>
 * The interpreter's verdict comes from running build/pp(.exe) on the err_ tests;
 * without a built interpreter only the second check runs.
 */
public class PipoHighlightingTest extends BasePlatformTestCase {
    private static Path repo() {
        return Path.of(System.getProperty("pipo.repo", "../..")).toAbsolutePath().normalize();
    }

    @Override
    protected String getTestDataPath() {
        return repo().toString();
    }

    @Override
    protected void setUp() throws Exception {
        super.setUp();
        myFixture.enableInspections(new PipoUnresolvedReferenceInspection());
    }

    /** the interpreter's stderr for a script, or null when there is no interpreter */
    private static String interpreterErrors(Path script) throws IOException, InterruptedException {
        Path exe = repo().resolve("build").resolve(File.separatorChar == '\\' ? "pp.exe" : "pp");
        if (!Files.isRegularFile(exe)) {
            return null;
        }
        Process p = new ProcessBuilder(exe.toString(), script.toString())
                .directory(script.getParent().toFile())
                .redirectOutput(ProcessBuilder.Redirect.DISCARD)
                .start();
        String err = new String(p.getErrorStream().readAllBytes(), StandardCharsets.UTF_8);
        if (!p.waitFor(10, TimeUnit.SECONDS)) {
            p.destroyForcibly();
        }
        return err;
    }

    public void testRepositoryHighlighting() throws Exception {
        for (String dir : List.of("lib", "tests", "examples")) {
            if (Files.isDirectory(repo().resolve(dir))) {
                myFixture.copyDirectoryToProject(dir, dir);
            }
        }

        List<Path> files = new ArrayList<>();
        for (String dir : List.of("lib", "tests", "examples")) {
            Path d = repo().resolve(dir);
            if (Files.isDirectory(d)) {
                try (Stream<Path> s = Files.walk(d)) {
                    s.filter(p -> p.toString().endsWith(".pp")).sorted().forEach(files::add);
                }
            }
        }

        List<String> problems = new ArrayList<>();
        List<String> unresolved = new ArrayList<>();
        List<String> exampleWarnings = new ArrayList<>();
        int syntaxChecked = 0;
        for (Path p : files) {
            String rel = repo().relativize(p).toString().replace('\\', '/');
            boolean errTest = p.getFileName().toString().startsWith("err_");
            String verdict = errTest ? interpreterErrors(p) : null;
            boolean syntaxError = verdict != null && verdict.contains("syntax error");

            myFixture.configureFromTempProjectFile(rel);
            String text = myFixture.getEditor().getDocument().getText();
            List<String> errors = new ArrayList<>();
            for (HighlightInfo info : myFixture.doHighlighting()) {
                int line = StringUtil.offsetToLineNumber(text, info.getStartOffset()) + 1;
                String where = rel + ":" + line + ": " + info.getDescription();
                if (info.getSeverity() == HighlightSeverity.ERROR) {
                    errors.add(where);
                } else if (info.getSeverity() == HighlightSeverity.WARNING && !errTest) {
                    // examples/ may lag behind the interpreter; only tests/ and lib/ must be clean
                    (rel.startsWith("examples/") ? exampleWarnings : unresolved).add(where);
                }
            }

            if (syntaxError) {
                syntaxChecked++;
                if (errors.isEmpty()) {
                    problems.add(rel + ": the interpreter reports a syntax error, the plugin none: " + verdict.trim());
                }
            } else if (!errors.isEmpty()) {
                problems.addAll(errors);
            }
        }

        System.out.println("syntax-error tests checked against the interpreter: " + syntaxChecked);
        System.out.println("warnings outside err_ tests:\n" + String.join("\n", unresolved));
        assertTrue(String.join("\n", problems), problems.isEmpty());
        assertTrue("unexpected warnings:\n" + String.join("\n", unresolved), unresolved.isEmpty());
    }
}
