package com.pipo.lang.psi;

import com.intellij.icons.AllIcons;
import com.intellij.lang.ASTNode;
import com.intellij.psi.PsiElement;
import com.pipo.lang.lexer.PipoTokenTypes;
import com.pipo.lang.parser.PipoElementTypes;
import org.jetbrains.annotations.NotNull;
import org.jetbrains.annotations.Nullable;

import javax.swing.Icon;

/** type T = ..., native type T<K, V> */
public final class PipoTypeDecl extends PipoNamedElement {
    public PipoTypeDecl(@NotNull ASTNode node) {
        super(node);
    }

    public boolean isNative() {
        return child(PipoTokenTypes.NATIVE_KW) != null;
    }

    /** the right-hand side of `=` */
    public @Nullable PsiElement getDefinition() {
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

    public boolean isCallback() {
        return getDefinition() instanceof PipoElement e && e.type() == PipoElementTypes.CALLBACK_TYPE;
    }

    public boolean isRecord() {
        return getDefinition() instanceof PipoElement e && e.type() == PipoElementTypes.RECORD_TYPE;
    }

    @Override
    public @NotNull String kindName() {
        return "type";
    }

    @Override
    public @NotNull Icon kindIcon() {
        return isCallback() ? AllIcons.Nodes.Lambda : isRecord() ? AllIcons.Nodes.Record : AllIcons.Nodes.Class;
    }
}
