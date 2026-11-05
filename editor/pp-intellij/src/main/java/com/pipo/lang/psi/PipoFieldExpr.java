package com.pipo.lang.psi;

import com.intellij.lang.ASTNode;
import com.intellij.openapi.util.TextRange;
import com.intellij.psi.PsiElement;
import com.intellij.psi.PsiReference;
import com.pipo.lang.lexer.PipoTokenTypes;
import com.pipo.lang.resolve.PipoReferences;
import org.jetbrains.annotations.NotNull;
import org.jetbrains.annotations.Nullable;

/** qualifier:name -- an object field, a module export or an enum variant */
public final class PipoFieldExpr extends PipoElement {
    public PipoFieldExpr(@NotNull ASTNode node) {
        super(node);
    }

    public @Nullable PsiElement getQualifier() {
        PsiElement first = getFirstChild();
        return first instanceof PipoElement ? first : null;
    }

    private @Nullable PsiElement colon() {
        PsiElement colon = null;
        for (PsiElement c = getFirstChild(); c != null; c = c.getNextSibling()) {
            if (PipoPsiUtil.is(c, PipoTokenTypes.COLON)) {
                colon = c;
            }
        }
        return colon;
    }

    /** the first token of the name after ':' */
    public @Nullable PsiElement getNameElement() {
        PsiElement colon = colon();
        PsiElement n = colon != null ? colon.getNextSibling() : null;
        while (n != null && !(n.getNode().getElementType() instanceof com.pipo.lang.lexer.PipoTokenType)) {
            n = n.getNextSibling(); // skip whitespace / comments
        }
        return n;
    }

    /** the range of the name within this element */
    public @Nullable TextRange getNameRange() {
        PsiElement n = getNameElement();
        if (n == null) {
            return null;
        }
        return new TextRange(n.getStartOffsetInParent(), getTextLength());
    }

    public @Nullable String getFieldName() {
        TextRange r = getNameRange();
        return r != null ? r.substring(getText()) : null;
    }

    @Override
    public PsiReference getReference() {
        return getNameRange() != null ? new PipoReferences.FieldRef(this) : null;
    }
}
