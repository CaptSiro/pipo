package com.pipo.lang.lexer;

import com.intellij.psi.tree.IElementType;
import com.pipo.lang.PipoLanguage;
import org.jetbrains.annotations.NonNls;
import org.jetbrains.annotations.NotNull;

public final class PipoTokenType extends IElementType {
    public PipoTokenType(@NotNull @NonNls String debugName) {
        super(debugName, PipoLanguage.INSTANCE);
    }

    @Override
    public String toString() {
        return "PipoToken." + super.toString();
    }
}
