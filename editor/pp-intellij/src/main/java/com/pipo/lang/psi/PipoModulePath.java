package com.pipo.lang.psi;

import com.intellij.lang.ASTNode;
import com.intellij.psi.PsiElement;
import com.intellij.psi.PsiReference;
import com.pipo.lang.lexer.PipoTokenTypes;
import com.pipo.lang.resolve.PipoReferences;
import org.jetbrains.annotations.NotNull;
import org.jetbrains.annotations.Nullable;

import java.util.ArrayList;
import java.util.List;

/** io, "./geometry", os:fs, "./ui":shapes */
public final class PipoModulePath extends PipoElement {
    public PipoModulePath(@NotNull ASTNode node) {
        super(node);
    }

    /** the STRING or IDENT the path starts with */
    public @Nullable PsiElement getFirstSegment() {
        for (PsiElement c = getFirstChild(); c != null; c = c.getNextSibling()) {
            if (PipoPsiUtil.is(c, PipoTokenTypes.STRING) || PipoPsiUtil.is(c, PipoTokenTypes.IDENT)) {
                return c;
            }
        }
        return null;
    }

    public @Nullable PsiElement getLastSegment() {
        PsiElement last = null;
        for (PsiElement c = getFirstChild(); c != null; c = c.getNextSibling()) {
            if (PipoPsiUtil.is(c, PipoTokenTypes.STRING) || PipoPsiUtil.is(c, PipoTokenTypes.IDENT)) {
                last = c;
            }
        }
        return last;
    }

    public boolean isFile() {
        return PipoPsiUtil.is(getFirstSegment(), PipoTokenTypes.STRING);
    }

    /** the submodule names after ':' */
    public List<String> getSubNames() {
        List<String> out = new ArrayList<>();
        PsiElement first = getFirstSegment();
        for (PsiElement c = first != null ? first.getNextSibling() : null; c != null; c = c.getNextSibling()) {
            if (PipoPsiUtil.is(c, PipoTokenTypes.IDENT)) {
                out.add(c.getText());
            }
        }
        return out;
    }

    public static @NotNull String unquote(@NotNull String s) {
        if (s.length() >= 2 && (s.charAt(0) == '"' || s.charAt(0) == '\'')) {
            return s.substring(1, s.charAt(s.length() - 1) == s.charAt(0) ? s.length() - 1 : s.length());
        }
        return s;
    }

    @Override
    public PsiReference getReference() {
        return getFirstSegment() != null ? new PipoReferences.ModuleRef(this) : null;
    }
}
