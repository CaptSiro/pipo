package com.pipo.lang;

import com.intellij.lang.Language;

public final class PipoLanguage extends Language {
    public static final PipoLanguage INSTANCE = new PipoLanguage();

    private PipoLanguage() {
        super("Pipo");
    }

    @Override
    public String getDisplayName() {
        return "PiPo";
    }
}
