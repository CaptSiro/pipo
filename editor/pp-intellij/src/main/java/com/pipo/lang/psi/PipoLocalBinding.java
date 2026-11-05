package com.pipo.lang.psi;

import com.intellij.icons.AllIcons;
import com.intellij.lang.ASTNode;
import com.pipo.lang.parser.PipoElementTypes;
import org.jetbrains.annotations.NotNull;

import javax.swing.Icon;

/**
 * A local name that is neither a parameter nor a let / const: a loop variable
 * (for x, y in ...) or a name bound by a pattern (some(x) -> ..., if let some(x) = ...).
 */
public final class PipoLocalBinding extends PipoNamedElement {
    public PipoLocalBinding(@NotNull ASTNode node) {
        super(node);
    }

    public boolean isLoopVariable() {
        return type() == PipoElementTypes.FOR_VAR;
    }

    @Override
    public @NotNull String kindName() {
        return isLoopVariable() ? "loop variable" : "pattern variable";
    }

    @Override
    public @NotNull Icon kindIcon() {
        return AllIcons.Nodes.Variable;
    }
}
