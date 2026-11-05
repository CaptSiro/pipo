package com.pipo.lang.psi;

import com.intellij.icons.AllIcons;
import com.intellij.lang.ASTNode;
import com.intellij.psi.PsiElement;
import com.pipo.lang.parser.PipoElementTypes;
import org.jetbrains.annotations.NotNull;
import org.jetbrains.annotations.Nullable;

import javax.swing.Icon;

/** int x in { int x, string name } */
public final class PipoRecordField extends PipoNamedElement {
    public PipoRecordField(@NotNull ASTNode node) {
        super(node);
    }

    public @Nullable PsiElement getTypeElement() {
        return child(PipoElementTypes.TYPES);
    }

    @Override
    public @NotNull String presentableText() {
        PsiElement t = getTypeElement();
        return (t != null ? PipoPsiUtil.collapse(t.getText()) + " " : "") + super.presentableText();
    }

    @Override
    public @NotNull String kindName() {
        return "field";
    }

    @Override
    public @NotNull Icon kindIcon() {
        return AllIcons.Nodes.Field;
    }
}
