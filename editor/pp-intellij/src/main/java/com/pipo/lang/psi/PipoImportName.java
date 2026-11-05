package com.pipo.lang.psi;

import com.intellij.icons.AllIcons;
import com.intellij.lang.ASTNode;
import com.intellij.psi.PsiElement;
import com.intellij.psi.PsiReference;
import com.pipo.lang.resolve.PipoReferences;
import org.jetbrains.annotations.NotNull;

import javax.swing.Icon;

/** a in import "./x" -> { a, b }: binds a locally and refers to the module's export */
public final class PipoImportName extends PipoNamedElement {
    public PipoImportName(@NotNull ASTNode node) {
        super(node);
    }

    public PipoImportStmt getImport() {
        PsiElement p = getParent();
        return p != null && p.getParent() instanceof PipoImportStmt i ? i : null;
    }

    @Override
    public PsiReference getReference() {
        return getNameIdentifier() != null ? new PipoReferences.ImportNameRef(this) : null;
    }

    @Override
    public @NotNull String kindName() {
        return "imported name";
    }

    @Override
    public @NotNull Icon kindIcon() {
        return AllIcons.Nodes.Include;
    }
}
