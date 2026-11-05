package com.pipo.lang;

import com.intellij.openapi.util.text.StringUtil;
import com.intellij.psi.PsiErrorElement;
import com.intellij.psi.PsiFile;
import com.intellij.psi.PsiFileFactory;
import com.intellij.psi.util.PsiTreeUtil;
import com.intellij.testFramework.fixtures.BasePlatformTestCase;
import com.pipo.lang.lexer.PipoLexer;
import com.pipo.lang.lexer.PipoTokenTypes;

import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.ArrayList;
import java.util.List;
import java.util.stream.Stream;

/**
 * Parses every .pp file of the language repository (tests/, lib/, examples/) and
 * the bundled stubs, and expects no syntax errors (the err_ tests excepted).
 */
public class PipoParsingTest extends BasePlatformTestCase {
    private static Path repo() {
        return Path.of(System.getProperty("pipo.repo", "../.."));
    }

    private List<String> errors(String name, String text) {
        PsiFile file = PsiFileFactory.getInstance(getProject()).createFileFromText(name, PipoFileType.INSTANCE, text);
        List<String> out = new ArrayList<>();
        for (PsiErrorElement e : PsiTreeUtil.findChildrenOfType(file, PsiErrorElement.class)) {
            int line = StringUtil.offsetToLineNumber(text, e.getTextOffset()) + 1;
            out.add(name + ":" + line + ": " + e.getErrorDescription());
        }
        return out;
    }

    public void testRepositoryFilesParse() throws IOException {
        List<Path> files = new ArrayList<>();
        for (String dir : List.of("tests", "lib", "examples")) {
            Path d = repo().resolve(dir);
            if (Files.isDirectory(d)) {
                try (Stream<Path> s = Files.walk(d)) {
                    s.filter(p -> p.toString().endsWith(".pp")).forEach(files::add);
                }
            }
        }
        assertFalse("no .pp files found under " + repo().toAbsolutePath(), files.isEmpty());

        List<String> problems = new ArrayList<>();
        int checked = 0;
        for (Path p : files) {
            if (p.getFileName().toString().startsWith("err_")) {
                continue; // deliberately broken: PipoHighlightingTest compares them with the interpreter
            }
            String text = Files.readString(p, StandardCharsets.UTF_8).replace("\r\n", "\n");
            problems.addAll(errors(repo().relativize(p).toString(), text));
            checked++;
        }
        assertTrue("checked " + checked + " files:\n" + String.join("\n", problems), problems.isEmpty());
    }

    public void testBundledStubsParse() throws IOException {
        for (String r : List.of("/pipo/builtins.pp", "/pipo/modules/io.pp", "/pipo/modules/os.pp", "/pipo/modules/pp.pp")) {
            String text = new String(getClass().getResourceAsStream(r).readAllBytes(), StandardCharsets.UTF_8);
            List<String> problems = errors(r, text.replace("\r\n", "\n"));
            assertTrue(String.join("\n", problems), problems.isEmpty());
        }
    }

    private List<String> tokens(String text) {
        PipoLexer lexer = new PipoLexer();
        lexer.start(text, 0, text.length(), 0);
        List<String> out = new ArrayList<>();
        while (lexer.getTokenType() != null) {
            if (lexer.getTokenType() != PipoTokenTypes.WHITE_SPACE) {
                out.add(lexer.getTokenText());
            }
            lexer.advance();
        }
        return out;
    }

    public void testOperatorSplitting() {
        assertEquals(List.of("x", "=", "-", "1"), tokens("x=-1"));
        assertEquals(List.of("a", "==", "-", "b"), tokens("a==-b"));
        assertEquals(List.of("x", "+=", "1"), tokens("x+=1"));
        assertEquals(List.of("Map", "<", "K", ",", "Map", "<", "K", ",", "V", ">", ">"), tokens("Map<K, Map<K, V>>"));
        assertEquals(List.of("Option", "<", "int", ">", "?"), tokens("Option<int>?"));
        assertEquals(List.of("...", "10"), tokens("...10"));
        assertEquals(List.of("0", "...=", "5"), tokens("0...=5"));
        assertEquals(List.of("x", "->", "x"), tokens("x->x"));
        assertEquals(List.of("a", ".", "f", "(", ")"), tokens("a.f()"));
        assertEquals(List.of("1.5", "+", "2e3"), tokens("1.5+2e3"));
        // an operator the file declares is not split
        assertEquals(List.of("function", "<>", "(", ")", "a", "<>", "b"), tokens("function <>() a <> b"));
        assertEquals(List.of("x", "/", "y", "// c"), tokens("x/y// c"));
    }
}
