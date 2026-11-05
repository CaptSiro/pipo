package com.pipo.lang.editor;

import com.intellij.lang.cacheBuilder.DefaultWordsScanner;
import com.intellij.lang.cacheBuilder.WordsScanner;
import com.intellij.lang.findUsages.FindUsagesProvider;
import com.intellij.psi.PsiElement;
import com.intellij.psi.tree.TokenSet;
import com.pipo.lang.lexer.PipoLexer;
import com.pipo.lang.lexer.PipoTokenTypes;
import com.pipo.lang.psi.PipoNamedElement;
import org.jetbrains.annotations.NotNull;

public final class PipoFindUsagesProvider implements FindUsagesProvider {
    @Override
    public WordsScanner getWordsScanner() {
        return new DefaultWordsScanner(new PipoLexer(), TokenSet.create(PipoTokenTypes.IDENT), PipoTokenTypes.COMMENTS,
                PipoTokenTypes.STRINGS);
    }

    @Override
    public boolean canFindUsagesFor(@NotNull PsiElement element) {
        return element instanceof PipoNamedElement n && n.getName() != null;
    }

    @Override
    public String getHelpId(@NotNull PsiElement element) {
        return null;
    }

    @Override
    public @NotNull String getType(@NotNull PsiElement element) {
        return element instanceof PipoNamedElement n ? n.kindName() : "";
    }

    @Override
    public @NotNull String getDescriptiveName(@NotNull PsiElement element) {
        return element instanceof PipoNamedElement n && n.getName() != null ? n.getName() : "";
    }

    @Override
    public @NotNull String getNodeText(@NotNull PsiElement element, boolean useFullName) {
        return element instanceof PipoNamedElement n ? n.presentableText() : element.getText();
    }
}
