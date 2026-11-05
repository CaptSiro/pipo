package com.pipo.lang.psi;

import com.intellij.psi.PsiComment;
import com.intellij.psi.PsiElement;
import com.intellij.psi.PsiFile;
import com.intellij.psi.PsiWhiteSpace;
import com.intellij.psi.tree.IElementType;
import com.pipo.lang.lexer.PipoTokenTypes;
import com.pipo.lang.parser.PipoElementTypes;
import org.jetbrains.annotations.NotNull;
import org.jetbrains.annotations.Nullable;

import java.util.ArrayList;
import java.util.Collections;
import java.util.List;

public final class PipoPsiUtil {
    private PipoPsiUtil() {
    }

    public static @NotNull String collapse(@NotNull String s) {
        return s.replaceAll("\\s+", " ").trim();
    }

    public static IElementType typeOf(@Nullable PsiElement e) {
        return e != null && e.getNode() != null ? e.getNode().getElementType() : null;
    }

    public static boolean is(@Nullable PsiElement e, IElementType t) {
        return typeOf(e) == t;
    }

    /** the declaration inside `export <declaration>`, or the statement itself */
    public static PsiElement unwrapExport(PsiElement s) {
        if (is(s, PipoElementTypes.EXPORT_STMT)) {
            for (PsiElement c = s.getFirstChild(); c != null; c = c.getNextSibling()) {
                if (c instanceof PipoNamedElement) {
                    return c;
                }
            }
        }
        return s;
    }

    /** statements of a file or block, with exported declarations unwrapped */
    public static List<PsiElement> statements(PsiElement scope) {
        List<PsiElement> out = new ArrayList<>();
        for (PsiElement c = scope.getFirstChild(); c != null; c = c.getNextSibling()) {
            if (c instanceof PipoElement) {
                out.add(c);
            }
        }
        return out;
    }

    public static List<PipoImportStmt> imports(PsiFile file) {
        List<PipoImportStmt> out = new ArrayList<>();
        if (file == null) {
            return out;
        }
        for (PsiElement c = file.getFirstChild(); c != null; c = c.getNextSibling()) {
            if (c instanceof PipoImportStmt i) {
                out.add(i);
            }
        }
        return out;
    }

    /** named declarations directly in the file (exported or not) */
    public static List<PipoNamedElement> topLevelDeclarations(PsiFile file) {
        List<PipoNamedElement> out = new ArrayList<>();
        for (PsiElement s : statements(file)) {
            if (unwrapExport(s) instanceof PipoNamedElement n && !(n instanceof PipoImportStmt)) {
                out.add(n);
            }
        }
        return out;
    }

    /** the statement (direct child of a block or file) containing e */
    public static @Nullable PsiElement statementOf(PsiElement e) {
        PsiElement cur = e;
        while (cur != null && !(cur instanceof PsiFile)) {
            PsiElement parent = cur.getParent();
            if (parent instanceof PsiFile || is(parent, PipoElementTypes.BLOCK)) {
                return cur;
            }
            cur = parent;
        }
        return null;
    }

    /** the `// ...` comment lines written directly above a declaration (no blank line between) */
    public static List<String> docComment(PsiElement decl) {
        PsiElement anchor = decl;
        if (is(decl.getParent(), PipoElementTypes.EXPORT_STMT)) {
            anchor = decl.getParent();
        }
        List<String> lines = new ArrayList<>();
        PsiElement prev = anchor.getPrevSibling();
        while (prev != null) {
            if (prev instanceof PsiWhiteSpace) {
                if (countNewlines(prev.getText()) > 1) {
                    break;
                }
            } else if (prev instanceof PsiComment) {
                String t = prev.getText();
                if (is(prev, PipoTokenTypes.LINE_COMMENT)) {
                    lines.add(t.substring(2).trim());
                } else {
                    String body = t.startsWith("/*") ? t.substring(2) : t;
                    if (body.endsWith("*/")) {
                        body = body.substring(0, body.length() - 2);
                    }
                    List<String> block = new ArrayList<>();
                    for (String line : body.split("\n")) {
                        block.add(line.trim().replaceFirst("^\\* ?", ""));
                    }
                    Collections.reverse(block);
                    lines.addAll(block);
                }
            } else {
                break;
            }
            prev = prev.getPrevSibling();
        }
        Collections.reverse(lines);
        while (!lines.isEmpty() && lines.get(0).isBlank()) {
            lines.remove(0);
        }
        // a section header like "// ---- trigonometry" describes a group, not the declaration
        if (!lines.isEmpty() && lines.get(0).startsWith("----")) {
            lines.remove(0);
        }
        return lines;
    }

    private static int countNewlines(String s) {
        int n = 0;
        for (int i = 0; i < s.length(); i++) {
            if (s.charAt(i) == '\n') {
                n++;
            }
        }
        return n;
    }

    /** the callee of a CALL_EXPR / METHOD_CALL_EXPR whose argument list is args */
    public static @Nullable PsiElement calleeOfArgs(PsiElement args) {
        PsiElement call = args.getParent();
        if (is(call, PipoElementTypes.CALL_EXPR)) {
            return call.getFirstChild();
        }
        if (is(call, PipoElementTypes.METHOD_CALL_EXPR)) {
            return methodCallee(call);
        }
        return null;
    }

    /** the f (or mod:f) in recv.f(args) */
    public static @Nullable PsiElement methodCallee(PsiElement methodCall) {
        boolean afterDot = false;
        for (PsiElement c = methodCall.getFirstChild(); c != null; c = c.getNextSibling()) {
            if (is(c, PipoTokenTypes.DOT)) {
                afterDot = true;
            } else if (afterDot && c instanceof PipoElement) {
                return c;
            }
        }
        return null;
    }

    /** is e the callee of a call: f(x), f x, recv.f(x), mod:f(x) */
    public static boolean isCallee(PsiElement e) {
        PsiElement p = e.getParent();
        if (is(p, PipoElementTypes.CALL_EXPR)) {
            return p.getFirstChild() == e;
        }
        if (is(p, PipoElementTypes.METHOD_CALL_EXPR)) {
            return methodCallee(p) == e;
        }
        if (is(p, PipoElementTypes.FIELD_EXPR) && p.getFirstChild() == e) {
            // the `mod` in recv.mod:f(x) is not a callee
            return false;
        }
        return false;
    }

    public static boolean isInBuiltinStub(PsiElement e) {
        PsiFile f = e.getContainingFile();
        return f != null && f.getUserData(PipoKeys.STUB) != null;
    }
}
