package com.pipo.lang.resolve;

import com.intellij.openapi.util.Key;
import com.intellij.psi.PsiDirectory;
import com.intellij.psi.PsiElement;
import com.intellij.psi.PsiFile;
import com.intellij.psi.PsiReference;
import com.intellij.psi.SmartPointerManager;
import com.intellij.psi.SmartPsiElementPointer;
import com.intellij.refactoring.move.moveFilesOrDirectories.MoveFileHandler;
import com.intellij.usageView.UsageInfo;
import com.pipo.lang.psi.PipoFile;
import com.pipo.lang.psi.PipoImportStmt;
import com.pipo.lang.psi.PipoModulePath;
import com.pipo.lang.psi.PipoPsiUtil;
import org.jetbrains.annotations.NotNull;

import java.util.ArrayList;
import java.util.List;
import java.util.Map;

/**
 * Keeps the imports of a moved .pp file working. `import "./x"` is relative to
 * the importing file, so after a move it would point somewhere else: before the
 * move each import's target is remembered, afterwards the path is rewritten to
 * reach the same file from the new folder. Imports of the moved file from other
 * files are updated by ModuleRef.bindToElement.
 */
public final class PipoMoveFileHandler extends MoveFileHandler {
    private record Target(SmartPsiElementPointer<PipoModulePath> path, SmartPsiElementPointer<PsiFile> file) {
    }

    private static final Key<List<Target>> TARGETS = Key.create("pipo.move.targets");

    @Override
    public boolean canProcessElement(PsiFile element) {
        return element instanceof PipoFile;
    }

    @Override
    public void prepareMovedFile(PsiFile file, PsiDirectory moveDestination, Map<PsiElement, PsiElement> oldToNewMap) {
        SmartPointerManager pointers = SmartPointerManager.getInstance(file.getProject());
        List<Target> targets = new ArrayList<>();
        for (PipoImportStmt imp : PipoPsiUtil.imports(file)) {
            PipoModulePath path = imp.getModulePath();
            if (path == null || !path.isFile()) {
                continue; // `import name` is found through the library directories, wherever the file is
            }
            PsiFile target = PipoModules.resolve(imp);
            if (target != null && target.getVirtualFile() != null) {
                targets.add(new Target(pointers.createSmartPsiElementPointer(path),
                        pointers.createSmartPsiElementPointer(target)));
            }
        }
        file.putUserData(TARGETS, targets);
    }

    @Override
    public List<UsageInfo> findUsages(PsiFile psiFile, PsiDirectory newParent, boolean searchInComments,
                                      boolean searchInNonJavaFiles) {
        return null; // references to the file are found and rebound by the platform
    }

    @Override
    public void retargetUsages(List<? extends UsageInfo> usageInfos, Map<PsiElement, PsiElement> oldToNewMap) {
    }

    @Override
    public void updateMovedFile(PsiFile file) {
        List<Target> targets = file.getUserData(TARGETS);
        file.putUserData(TARGETS, null);
        if (targets == null) {
            return;
        }
        for (Target t : targets) {
            PipoModulePath path = t.path().getElement();
            PsiFile target = t.file().getElement();
            PsiReference ref = path != null ? path.getReference() : null;
            if (ref != null && target != null) {
                ref.bindToElement(target);
            }
        }
    }
}
