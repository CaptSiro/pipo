package com.pipo.lang.psi;

import com.intellij.icons.AllIcons;
import com.intellij.lang.ASTNode;
import com.intellij.psi.PsiElement;
import com.pipo.lang.parser.PipoElementTypes;
import org.jetbrains.annotations.NotNull;
import org.jetbrains.annotations.Nullable;

import javax.swing.Icon;

/** a parameter; in a declaration or interface requirement it may have only a type */
public final class PipoParam extends PipoNamedElement {
    public PipoParam(@NotNull ASTNode node) {
        super(node);
    }

    public @Nullable PsiElement getTypeElement() {
        return child(PipoElementTypes.TYPES);
    }

    @Override
    public @NotNull String presentableText() {
        PsiElement t = getTypeElement();
        String n = getName();
        String type = t != null ? PipoPsiUtil.collapse(t.getText()) : "";
        if (n == null) {
            return type.isEmpty() ? "?" : type;
        }
        return type.isEmpty() ? n : type + " " + n;
    }

    @Override
    public @NotNull String kindName() {
        return "parameter";
    }

    @Override
    public @NotNull Icon kindIcon() {
        return AllIcons.Nodes.Parameter;
    }
}
