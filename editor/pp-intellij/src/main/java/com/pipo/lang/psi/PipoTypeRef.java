package com.pipo.lang.psi;

import com.intellij.lang.ASTNode;
import com.intellij.psi.PsiElement;
import com.intellij.psi.PsiReference;
import com.pipo.lang.lexer.PipoTokenTypes;
import com.pipo.lang.parser.PipoBuiltinNames;
import com.pipo.lang.resolve.PipoReferences;
import org.jetbrains.annotations.NotNull;
import org.jetbrains.annotations.Nullable;

/**
 * A type name: TYPE_REF covers int, Person, Error&lt;int&gt;, geometry:Vec, tuple&lt;int, string&gt;.
 * TYPE_QUALIFIER (the geometry in geometry:Vec) uses this class too.
 */
public final class PipoTypeRef extends PipoElement {
    public PipoTypeRef(@NotNull ASTNode node) {
        super(node);
    }

    public boolean isQualifierPart() {
        return type() == com.pipo.lang.parser.PipoElementTypes.TYPE_QUALIFIER;
    }

    /** the identifier naming the type (or, for a qualifier, the module) */
    public @Nullable PsiElement getNameLeaf() {
        return child(PipoTokenTypes.IDENT);
    }

    public @Nullable String getTypeName() {
        PsiElement n = getNameLeaf();
        return n != null ? n.getText() : null;
    }

    /** the module part: geometry in geometry:Vec, or the outer part of a:b */
    public @Nullable PipoTypeRef getQualifier() {
        PsiElement q = child(com.pipo.lang.parser.PipoElementTypes.TYPE_QUALIFIER);
        return q instanceof PipoTypeRef t ? t : null;
    }

    public boolean isBuiltin() {
        String n = getTypeName();
        return !isQualifierPart() && getQualifier() == null && n != null
                && (PipoBuiltinNames.PRIMITIVES.contains(n) || n.equals("tuple"));
    }

    @Override
    public PsiReference getReference() {
        if (getNameLeaf() == null || isBuiltin()) {
            return null;
        }
        return new PipoReferences.TypeRef(this);
    }
}
