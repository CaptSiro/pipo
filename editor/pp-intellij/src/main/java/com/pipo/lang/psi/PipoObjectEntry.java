package com.pipo.lang.psi;

import com.intellij.icons.AllIcons;
import com.intellij.lang.ASTNode;
import com.intellij.psi.PsiElement;
import com.intellij.util.IncorrectOperationException;
import com.pipo.lang.lexer.PipoTokenTypes;
import org.jetbrains.annotations.NotNull;
import org.jetbrains.annotations.Nullable;

import javax.swing.Icon;

/** key: value in an object literal, or the shorthand { name } */
public final class PipoObjectEntry extends PipoNamedElement {
    public PipoObjectEntry(@NotNull ASTNode node) {
        super(node);
    }

    public boolean isShorthand() {
        return getFirstChild() instanceof PipoRefExpr;
    }

    @Override
    public @Nullable PsiElement getNameIdentifier() {
        return getFirstChild();
    }

    @Override
    public @Nullable String getName() {
        PsiElement key = getFirstChild();
        return key != null ? PipoModulePath.unquote(key.getText()) : null;
    }

    @Override
    public PsiElement setName(@NotNull String name) throws IncorrectOperationException {
        PsiElement key = getFirstChild();
        if (key == null || isShorthand()) {
            throw new IncorrectOperationException("cannot rename a shorthand entry");
        }
        if (PipoPsiUtil.is(key, PipoTokenTypes.STRING)) {
            char q = key.getText().charAt(0);
            PsiElement lit = PipoElementFactory.createFile(getProject(), q + name + q).getFirstChild();
            key.replace(lit.getFirstChild().getFirstChild());
        } else {
            key.replace(PipoElementFactory.createIdentifier(getProject(), name));
        }
        return this;
    }

    /** the value expression (the reference itself for a shorthand entry) */
    public @Nullable PsiElement getValue() {
        if (isShorthand()) {
            return getFirstChild();
        }
        PsiElement colon = child(PipoTokenTypes.COLON);
        for (PsiElement c = colon != null ? colon.getNextSibling() : null; c != null; c = c.getNextSibling()) {
            if (c instanceof PipoElement) {
                return c;
            }
        }
        return null;
    }

    @Override
    public @NotNull String kindName() {
        return "property";
    }

    @Override
    public @NotNull Icon kindIcon() {
        return AllIcons.Nodes.Property;
    }
}
