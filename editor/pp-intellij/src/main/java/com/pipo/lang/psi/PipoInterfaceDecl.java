package com.pipo.lang.psi;

import com.intellij.icons.AllIcons;
import com.intellij.lang.ASTNode;
import org.jetbrains.annotations.NotNull;

import javax.swing.Icon;
import java.util.List;

public final class PipoInterfaceDecl extends PipoNamedElement {
    public PipoInterfaceDecl(@NotNull ASTNode node) {
        super(node);
    }

    public List<PipoFunctionDecl> getRequirements() {
        return children(PipoFunctionDecl.class);
    }

    @Override
    public @NotNull String kindName() {
        return "interface";
    }

    @Override
    public @NotNull Icon kindIcon() {
        return AllIcons.Nodes.Interface;
    }
}
