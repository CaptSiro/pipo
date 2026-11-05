package com.pipo.lang.psi;

import com.intellij.extapi.psi.PsiFileBase;
import com.intellij.openapi.fileTypes.FileType;
import com.intellij.psi.FileViewProvider;
import com.pipo.lang.PipoFileType;
import com.pipo.lang.PipoLanguage;
import org.jetbrains.annotations.NotNull;

public final class PipoFile extends PsiFileBase {
    public PipoFile(@NotNull FileViewProvider viewProvider) {
        super(viewProvider, PipoLanguage.INSTANCE);
    }

    @Override
    public @NotNull FileType getFileType() {
        return PipoFileType.INSTANCE;
    }

    @Override
    public String toString() {
        return "PiPo file";
    }
}
