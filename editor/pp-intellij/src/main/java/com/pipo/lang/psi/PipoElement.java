package com.pipo.lang.psi;

import com.intellij.extapi.psi.ASTWrapperPsiElement;
import com.intellij.lang.ASTNode;
import com.intellij.psi.PsiElement;
import com.intellij.psi.tree.IElementType;
import com.intellij.psi.tree.TokenSet;
import org.jetbrains.annotations.NotNull;
import org.jetbrains.annotations.Nullable;

import java.util.ArrayList;
import java.util.List;

/** Base class of every composite PSI element. */
public class PipoElement extends ASTWrapperPsiElement {
    public PipoElement(@NotNull ASTNode node) {
        super(node);
    }

    public IElementType type() {
        return getNode().getElementType();
    }

    public @Nullable PsiElement child(IElementType type) {
        ASTNode n = getNode().findChildByType(type);
        return n != null ? n.getPsi() : null;
    }

    public @Nullable PsiElement child(TokenSet types) {
        ASTNode n = getNode().findChildByType(types);
        return n != null ? n.getPsi() : null;
    }

    public List<PsiElement> children(IElementType type) {
        List<PsiElement> out = new ArrayList<>();
        for (ASTNode n : getNode().getChildren(null)) {
            if (n.getElementType() == type) {
                out.add(n.getPsi());
            }
        }
        return out;
    }

    public <T> @Nullable T child(Class<T> cls) {
        for (PsiElement c = getFirstChild(); c != null; c = c.getNextSibling()) {
            if (cls.isInstance(c)) {
                return cls.cast(c);
            }
        }
        return null;
    }

    public <T> List<T> children(Class<T> cls) {
        List<T> out = new ArrayList<>();
        for (PsiElement c = getFirstChild(); c != null; c = c.getNextSibling()) {
            if (cls.isInstance(c)) {
                out.add(cls.cast(c));
            }
        }
        return out;
    }
}
