package com.pipo.lang;

import com.intellij.openapi.fileTypes.LanguageFileType;
import org.jetbrains.annotations.NotNull;

import javax.swing.Icon;

public final class PipoFileType extends LanguageFileType {
    public static final PipoFileType INSTANCE = new PipoFileType();
    public static final String EXTENSION = "pp";

    private PipoFileType() {
        super(PipoLanguage.INSTANCE);
    }

    @Override
    public @NotNull String getName() {
        return "PiPo";
    }

    @Override
    public @NotNull String getDescription() {
        return "PiPo source file";
    }

    @Override
    public @NotNull String getDefaultExtension() {
        return EXTENSION;
    }

    @Override
    public Icon getIcon() {
        return PipoIcons.FILE;
    }
}
