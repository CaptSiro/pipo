package com.pipo.lang.psi;

import com.intellij.icons.AllIcons;
import com.intellij.lang.ASTNode;
import org.jetbrains.annotations.NotNull;

import javax.swing.Icon;
import java.util.List;

public final class PipoEnumDecl extends PipoNamedElement {
    public PipoEnumDecl(@NotNull ASTNode node) {
        super(node);
    }

    public List<PipoEnumVariant> getVariants() {
        return children(PipoEnumVariant.class);
    }

    @Override
    public @NotNull String kindName() {
        return "enum";
    }

    @Override
    public @NotNull Icon kindIcon() {
        return AllIcons.Nodes.Enum;
    }
}
