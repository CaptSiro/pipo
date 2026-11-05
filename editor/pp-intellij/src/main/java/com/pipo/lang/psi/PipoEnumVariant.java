package com.pipo.lang.psi;

import com.intellij.icons.AllIcons;
import com.intellij.lang.ASTNode;
import com.intellij.psi.PsiElement;
import com.pipo.lang.lexer.PipoTokenTypes;
import org.jetbrains.annotations.NotNull;

import javax.swing.Icon;

public final class PipoEnumVariant extends PipoNamedElement {
    public PipoEnumVariant(@NotNull ASTNode node) {
        super(node);
    }

    /** some(T), none(): a constructor function under the variant's own name */
    public boolean isConstructor() {
        return child(PipoTokenTypes.LPAREN) != null;
    }

    public PipoEnumDecl getEnum() {
        return getParent() instanceof PipoEnumDecl e ? e : null;
    }

    @Override
    public @NotNull String presentableText() {
        PsiElement lp = child(PipoTokenTypes.LPAREN);
        if (lp == null) {
            return super.presentableText();
        }
        PsiElement rp = child(PipoTokenTypes.RPAREN);
        int end = rp != null ? rp.getStartOffsetInParent() + 1 : getTextLength();
        return super.presentableText() + PipoPsiUtil.collapse(getText().substring(lp.getStartOffsetInParent(), end));
    }

    @Override
    public @NotNull String kindName() {
        return "enum variant";
    }

    @Override
    public @NotNull Icon kindIcon() {
        return AllIcons.Nodes.Field;
    }
}
