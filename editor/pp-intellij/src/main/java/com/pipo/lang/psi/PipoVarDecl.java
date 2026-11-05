package com.pipo.lang.psi;

import com.intellij.icons.AllIcons;
import com.intellij.lang.ASTNode;
import com.intellij.psi.PsiElement;
import com.pipo.lang.lexer.PipoTokenTypes;
import com.pipo.lang.parser.PipoElementTypes;
import org.jetbrains.annotations.NotNull;
import org.jetbrains.annotations.Nullable;

import javax.swing.Icon;

/** let / const */
public final class PipoVarDecl extends PipoNamedElement {
    public PipoVarDecl(@NotNull ASTNode node) {
        super(node);
    }

    public boolean isConst() {
        return child(PipoTokenTypes.CONST) != null;
    }

    public @Nullable PsiElement getTypeElement() {
        PsiElement t = child(PipoElementTypes.TYPES);
        PsiElement name = getNameIdentifier();
        return t != null && (name == null || t.getTextOffset() < name.getTextOffset()) ? t : null;
    }

    public @Nullable PsiElement getInitializer() {
        PsiElement eq = child(PipoTokenTypes.ASSIGN);
        if (eq == null) {
            return null;
        }
        for (PsiElement c = eq.getNextSibling(); c != null; c = c.getNextSibling()) {
            if (c instanceof PipoElement) {
                return c;
            }
        }
        return null;
    }

    @Override
    public @NotNull String presentableText() {
        PsiElement t = getTypeElement();
        return (t != null ? PipoPsiUtil.collapse(t.getText()) + " " : "") + super.presentableText();
    }

    @Override
    public @NotNull String kindName() {
        return isConst() ? "constant" : "variable";
    }

    @Override
    public @NotNull Icon kindIcon() {
        return isConst() ? AllIcons.Nodes.Constant : AllIcons.Nodes.Variable;
    }
}
