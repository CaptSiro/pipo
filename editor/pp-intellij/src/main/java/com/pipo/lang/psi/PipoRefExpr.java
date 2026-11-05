package com.pipo.lang.psi;

import com.intellij.lang.ASTNode;
import com.intellij.psi.PsiReference;
import com.pipo.lang.lexer.PipoTokenTypes;
import com.pipo.lang.resolve.PipoReferences;
import org.jetbrains.annotations.NotNull;

/** a name used as a value: x, f, +, in, [] */
public final class PipoRefExpr extends PipoElement {
    public PipoRefExpr(@NotNull ASTNode node) {
        super(node);
    }

    public @NotNull String getReferencedName() {
        return getText();
    }

    public boolean isOperator() {
        return PipoPsiUtil.is(getFirstChild(), PipoTokenTypes.OP) || PipoPsiUtil.is(getFirstChild(), PipoTokenTypes.IN);
    }

    @Override
    public PsiReference getReference() {
        return new PipoReferences.NameRef(this);
    }
}
