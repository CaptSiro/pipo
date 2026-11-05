package com.pipo.lang.psi;

import com.intellij.openapi.project.Project;
import com.intellij.psi.PsiElement;
import com.intellij.psi.PsiFileFactory;
import com.intellij.psi.util.PsiTreeUtil;
import com.intellij.util.IncorrectOperationException;
import com.pipo.lang.PipoFileType;
import com.pipo.lang.lexer.PipoLexer;
import com.pipo.lang.lexer.PipoTokenTypes;
import com.pipo.lang.parser.PipoElementTypes;
import org.jetbrains.annotations.NotNull;

/** Creates PSI fragments by parsing small dummy files. */
public final class PipoElementFactory {
    private PipoElementFactory() {
    }

    public static PipoFile createFile(Project project, String text) {
        return (PipoFile) PsiFileFactory.getInstance(project).createFileFromText("dummy.pp", PipoFileType.INSTANCE, text);
    }

    public static boolean isIdentifier(String name) {
        if (name.isEmpty() || !PipoLexer.isIdentStart(name.charAt(0))) {
            return false;
        }
        for (int i = 1; i < name.length(); i++) {
            if (!PipoLexer.isIdentChar(name.charAt(i))) {
                return false;
            }
        }
        return !PipoTokenTypes.KEYWORDS.containsKey(name);
    }

    /** a NAME node holding name (an identifier or an operator) */
    public static @NotNull PsiElement createName(Project project, String name) {
        String text = isIdentifier(name) ? "let " + name : "function " + name + "() {}";
        PipoFile file = createFile(project, text);
        for (PsiElement e : PsiTreeUtil.collectElements(file, x -> PipoPsiUtil.is(x, PipoElementTypes.NAME))) {
            return e;
        }
        throw new IncorrectOperationException("'" + name + "' is not a valid name");
    }

    /** a single identifier token */
    public static @NotNull PsiElement createIdentifier(Project project, String name) {
        if (!isIdentifier(name)) {
            throw new IncorrectOperationException("'" + name + "' is not a valid identifier");
        }
        PipoFile file = createFile(project, name);
        PsiElement leaf = PsiTreeUtil.getDeepestFirst(file);
        if (!PipoPsiUtil.is(leaf, PipoTokenTypes.IDENT)) {
            throw new IncorrectOperationException("'" + name + "' is not a valid identifier");
        }
        return leaf;
    }
}
