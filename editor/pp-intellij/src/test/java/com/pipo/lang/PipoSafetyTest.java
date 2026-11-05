package com.pipo.lang;

import com.intellij.openapi.command.WriteCommandAction;
import com.intellij.psi.PsiFile;
import com.intellij.psi.codeStyle.CodeStyleManager;
import com.intellij.psi.impl.DebugUtil;
import com.intellij.testFramework.fixtures.BasePlatformTestCase;
import com.pipo.lang.lexer.PipoLexer;

import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.ArrayList;
import java.util.List;
import java.util.stream.Stream;

/**
 * Properties checked on every .pp file of the repository:
 * <ul>
 *   <li>reformatting keeps the meaning: the same tree (whitespace aside) and the same line breaks;</li>
 *   <li>the lexer gives the same tokens when the editor restarts it in the middle of a file.</li>
 * </ul>
 */
public class PipoSafetyTest extends BasePlatformTestCase {
    private static List<Path> repoFiles() throws IOException {
        Path repo = Path.of(System.getProperty("pipo.repo", "../.."));
        List<Path> out = new ArrayList<>();
        for (String dir : List.of("tests", "lib", "examples")) {
            Path d = repo.resolve(dir);
            if (Files.isDirectory(d)) {
                try (Stream<Path> s = Files.walk(d)) {
                    s.filter(p -> p.toString().endsWith(".pp") && !p.getFileName().toString().startsWith("err_"))
                            .sorted().forEach(out::add);
                }
            }
        }
        return out;
    }

    private static String read(Path p) throws IOException {
        return Files.readString(p, StandardCharsets.UTF_8).replace("\r\n", "\n");
    }

    private static String firstDifference(String a, String b) {
        String[] la = a.split("\n", -1);
        String[] lb = b.split("\n", -1);
        for (int i = 0; i < Math.min(la.length, lb.length); i++) {
            if (!la[i].trim().equals(lb[i].trim())) {
                return "line " + (i + 1) + ": [" + la[i].trim() + "] became [" + lb[i].trim() + "]";
            }
        }
        return "?";
    }

    private static int lines(String s) {
        return (int) s.chars().filter(c -> c == '\n').count();
    }

    public void testReformatKeepsMeaning() throws IOException {
        List<String> problems = new ArrayList<>();
        for (Path p : repoFiles()) {
            String text = read(p);
            PsiFile file = myFixture.configureByText("f" + problems.size() + ".pp", text);
            String before = DebugUtil.psiToString(file, false, false);
            WriteCommandAction.runWriteCommandAction(getProject(),
                    (Runnable) () -> CodeStyleManager.getInstance(getProject()).reformat(file));
            String after = DebugUtil.psiToString(file, false, false);
            String formatted = file.getText();
            if (!before.equals(after)) {
                problems.add(p.getFileName() + ": the tree changed: " + firstDifference(text, formatted));
            } else if (lines(text) != lines(formatted)) {
                problems.add(p.getFileName() + ": lines were joined or split");
            }
        }
        assertTrue(String.join("\n", problems), problems.isEmpty());
    }

    private static List<String> tokens(CharSequence text, int start) {
        PipoLexer lexer = new PipoLexer();
        lexer.start(text, start, text.length(), 0);
        List<String> out = new ArrayList<>();
        while (lexer.getTokenType() != null) {
            out.add(lexer.getTokenStart() + ":" + lexer.getTokenType() + ":" + lexer.getState());
            lexer.advance();
        }
        return out;
    }

    public void testLexerRestartsConsistently() throws IOException {
        for (Path p : repoFiles()) {
            String text = read(p);
            List<String> all = tokens(text, 0);
            // restart at every 7th token whose state is 0, as the highlighter does after an edit
            for (int i = 0; i < all.size(); i += 7) {
                String[] parts = all.get(i).split(":");
                if (!parts[parts.length - 1].equals("0")) {
                    continue;
                }
                int offset = Integer.parseInt(parts[0]);
                List<String> rest = tokens(text, offset);
                assertEquals(p.getFileName() + " restarted at " + offset, all.subList(i, all.size()), rest);
            }
        }
    }
}
