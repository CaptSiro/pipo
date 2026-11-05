package com.pipo.lang.resolve;

import com.intellij.openapi.project.Project;
import com.intellij.openapi.project.ProjectUtil;
import com.intellij.openapi.util.RecursionManager;
import com.intellij.openapi.vfs.LocalFileSystem;
import com.intellij.openapi.vfs.VirtualFile;
import com.intellij.openapi.vfs.VirtualFileManager;
import com.intellij.psi.PsiElement;
import com.intellij.psi.PsiFile;
import com.intellij.psi.PsiManager;
import com.intellij.psi.util.CachedValueProvider;
import com.intellij.psi.util.CachedValuesManager;
import com.intellij.psi.util.PsiModificationTracker;
import com.pipo.lang.PipoFileType;
import com.pipo.lang.psi.PipoImportStmt;
import com.pipo.lang.psi.PipoModulePath;
import com.pipo.lang.settings.PipoSettings;
import org.jetbrains.annotations.NotNull;
import org.jetbrains.annotations.Nullable;

import java.io.File;
import java.util.ArrayList;
import java.util.List;
import java.util.Set;
import java.util.TreeSet;

/**
 * Finds the file an import loads, like the interpreter does:
 * <ul>
 *   <li>{@code import "./path/file"}: relative to the importing file, ".pp" optional;</li>
 *   <li>{@code import name}: a native module (io, os, pp: bundled stubs), then name.pp in the
 *       library directories (project settings, the project's lib/, the interpreter's lib/, PP_PATH);</li>
 *   <li>{@code import a:b}: an export of a named b, or the file b.pp in the folder a.</li>
 * </ul>
 */
public final class PipoModules {
    private PipoModules() {
    }

    public static @Nullable PsiFile resolve(@NotNull PipoImportStmt imp) {
        return CachedValuesManager.getCachedValue(imp, () -> {
            PsiFile f = RecursionManager.doPreventingRecursion(imp, false, () -> compute(imp));
            return CachedValueProvider.Result.create(f, PsiModificationTracker.getInstance(imp.getProject()),
                    VirtualFileManager.VFS_STRUCTURE_MODIFICATIONS);
        });
    }

    private static @Nullable PsiFile compute(PipoImportStmt imp) {
        PipoModulePath path = imp.getModulePath();
        PsiElement first = path != null ? path.getFirstSegment() : null;
        if (first == null) {
            return null;
        }
        Project project = imp.getProject();
        PsiManager psi = PsiManager.getInstance(project);
        VirtualFile base = baseDir(imp.getContainingFile());

        VirtualFile vf = null;
        VirtualFile folder = null;
        PsiFile current = null;
        if (path.isFile()) {
            String rel = PipoModulePath.unquote(first.getText());
            if (rel.isEmpty()) {
                return null;
            }
            VirtualFile found = findRelative(base, rel);
            if (found != null && found.isDirectory()) {
                folder = found;
            } else {
                vf = found;
            }
        } else {
            // a registered native module first, then name.pp in the library directories
            // (not next to the importing file: that is what `import "./name"` is for)
            String name = first.getText();
            current = PipoBuiltins.getInstance(project).module(name);
            List<VirtualFile> dirs = current != null ? List.of() : libraryDirs(project);
            for (VirtualFile d : dirs) {
                VirtualFile c = d.findChild(name + ".pp");
                if (c != null && !c.isDirectory()) {
                    vf = c;
                    break;
                }
            }
            if (vf == null && current == null) {
                for (VirtualFile d : dirs) {
                    VirtualFile c = d.findChild(name);
                    if (c != null && c.isDirectory()) {
                        folder = c;
                        break;
                    }
                }
            }
        }
        if (vf != null) {
            current = psi.findFile(vf);
        }

        for (String sub : path.getSubNames()) {
            PsiFile next = null;
            if (current != null) {
                for (PsiElement e : PipoResolver.moduleExports(current, sub)) {
                    if (e instanceof PipoImportStmt i) {
                        next = resolve(i);
                        break;
                    }
                }
                VirtualFile cvf = current.getVirtualFile();
                if (next == null && cvf != null && cvf.getParent() != null && cvf.isInLocalFileSystem()) {
                    folder = cvf.getParent().findChild(cvf.getNameWithoutExtension());
                }
            }
            if (next == null && folder != null) {
                VirtualFile c = folder.findChild(sub + ".pp");
                if (c != null && !c.isDirectory()) {
                    next = psi.findFile(c);
                } else {
                    VirtualFile d = folder.findChild(sub);
                    folder = d != null && d.isDirectory() ? d : null;
                    current = null;
                    if (folder == null) {
                        return null;
                    }
                    continue;
                }
            }
            current = next;
            folder = null;
            if (current == null) {
                return null;
            }
        }
        return current;
    }

    private static @Nullable VirtualFile baseDir(@Nullable PsiFile file) {
        if (file == null) {
            return null;
        }
        VirtualFile vf = file.getOriginalFile().getVirtualFile();
        return vf != null ? vf.getParent() : null;
    }

    private static @Nullable VirtualFile findRelative(@Nullable VirtualFile base, String rel) {
        rel = rel.replace('\\', '/');
        if (new File(rel).isAbsolute()) {
            VirtualFile abs = LocalFileSystem.getInstance().findFileByPath(rel);
            if (abs == null && !rel.endsWith(".pp")) {
                abs = LocalFileSystem.getInstance().findFileByPath(rel + ".pp");
            }
            return abs;
        }
        if (base == null) {
            return null;
        }
        if (!rel.endsWith(".pp")) {
            VirtualFile withExt = base.findFileByRelativePath(rel + ".pp");
            if (withExt != null && !withExt.isDirectory()) {
                return withExt;
            }
        }
        return base.findFileByRelativePath(rel);
    }

    /** every module file loaded, directly or through other imports, when file runs (not file itself) */
    public static List<PsiFile> loadedModules(@NotNull PsiFile file) {
        return CachedValuesManager.getCachedValue(file, () -> {
            List<PsiFile> out = new ArrayList<>();
            java.util.ArrayDeque<PsiFile> queue = new java.util.ArrayDeque<>();
            queue.add(file);
            while (!queue.isEmpty() && out.size() < 64) {
                PsiFile current = queue.poll();
                for (PipoImportStmt imp : com.pipo.lang.psi.PipoPsiUtil.imports(current)) {
                    PsiFile m = resolve(imp);
                    if (m != null && m != file && !out.contains(m)) {
                        out.add(m);
                        queue.add(m);
                    }
                }
            }
            return CachedValueProvider.Result.create(out, PsiModificationTracker.getInstance(file.getProject()),
                    VirtualFileManager.VFS_STRUCTURE_MODIFICATIONS);
        });
    }

    /** directories searched for `import name`, in order */
    public static List<VirtualFile> libraryDirs(Project project) {
        PipoSettings settings = PipoSettings.getInstance(project);
        List<VirtualFile> out = new ArrayList<>();
        for (String p : settings.getLibraryPaths()) {
            addDir(out, p);
        }
        // the project's own lib/ (found through the VFS, so it works for any project root)
        VirtualFile projectDir = ProjectUtil.guessProjectDir(project);
        VirtualFile projectLib = projectDir != null ? projectDir.findChild("lib") : null;
        if (projectLib != null && projectLib.isDirectory() && !out.contains(projectLib)) {
            out.add(projectLib);
        }
        String interpreter = settings.findInterpreter();
        if (interpreter != null) {
            File parent = new File(interpreter).getParentFile();
            if (parent != null) {
                addDir(out, new File(parent, "lib").getPath());
            }
        }
        String ppPath = System.getenv("PP_PATH");
        if (ppPath != null) {
            for (String p : ppPath.split(File.pathSeparator)) {
                if (!p.isBlank()) {
                    addDir(out, p.trim());
                }
            }
        }
        return out;
    }

    private static void addDir(List<VirtualFile> out, String path) {
        VirtualFile d = LocalFileSystem.getInstance().findFileByPath(path.replace('\\', '/'));
        if (d != null && d.isDirectory() && !out.contains(d)) {
            out.add(d);
        }
    }

    /** module names usable in `import name` from file */
    public static Set<String> availableModules(PsiFile file) {
        Set<String> out = new TreeSet<>(PipoBuiltins.STUB_MODULES);
        List<VirtualFile> dirs = libraryDirs(file.getProject());
        VirtualFile self = file.getOriginalFile().getVirtualFile();
        for (VirtualFile d : dirs) {
            for (VirtualFile c : d.getChildren()) {
                if (!c.isDirectory() && PipoFileType.EXTENSION.equals(c.getExtension()) && !c.equals(self)) {
                    out.add(c.getNameWithoutExtension());
                }
            }
        }
        return out;
    }
}
