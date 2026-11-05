package com.pipo.lang.psi;

import com.intellij.icons.AllIcons;
import com.intellij.lang.ASTNode;
import org.jetbrains.annotations.NotNull;

import javax.swing.Icon;

/** <T> */
public final class PipoGenericParam extends PipoNamedElement {
    public PipoGenericParam(@NotNull ASTNode node) {
        super(node);
    }

    @Override
    public @NotNull String kindName() {
        return "type parameter";
    }

    @Override
    public @NotNull Icon kindIcon() {
        return AllIcons.Nodes.Parameter;
    }
}
