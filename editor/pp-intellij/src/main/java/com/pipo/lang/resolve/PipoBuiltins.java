package com.pipo.lang.resolve;

import com.intellij.openapi.components.Service;
import com.intellij.openapi.diagnostic.Logger;
import com.intellij.openapi.project.Project;
import com.intellij.psi.PsiFile;
import com.intellij.psi.PsiManager;
import com.intellij.testFramework.LightVirtualFile;
import com.pipo.lang.PipoFileType;
import com.pipo.lang.psi.PipoKeys;
import org.jetbrains.annotations.NotNull;
import org.jetbrains.annotations.Nullable;

import java.io.IOException;
import java.io.InputStream;
import java.nio.charset.StandardCharsets;
import java.util.Map;
import java.util.Set;
import java.util.concurrent.ConcurrentHashMap;

/**
 * Stubs bundled with the plugin, written in PiPo itself: the internal functions
 * (src/builtins.c and src/prelude.c) and the native modules that have no .pp file
 * (io, os, pp). They give completion, documentation and navigation for names the
 * interpreter defines in C. They open read-only.
 */
@Service(Service.Level.PROJECT)
public final class PipoBuiltins {
    public static final Set<String> STUB_MODULES = Set.of("io", "os", "pp");
    private static final Logger LOG = Logger.getInstance(PipoBuiltins.class);

    private final Project project;
    private final Map<String, Loaded> files = new ConcurrentHashMap<>();

    private record Loaded(LightVirtualFile file, PsiFile psi) {
    }

    public PipoBuiltins(@NotNull Project project) {
        this.project = project;
    }

    public static PipoBuiltins getInstance(@NotNull Project project) {
        return project.getService(PipoBuiltins.class);
    }

    public @Nullable PsiFile builtinsFile() {
        return load("builtins", "/pipo/builtins.pp", "");
    }

    public @Nullable PsiFile module(@NotNull String name) {
        if (!STUB_MODULES.contains(name)) {
            return null;
        }
        return load(name, "/pipo/modules/" + name + ".pp", name);
    }

    /* synchronized: two threads must never create two different PSI files for one stub (resolve results are cached) */
    private synchronized @Nullable PsiFile load(String fileName, String resource, String stubName) {
        Loaded l = files.get(fileName);
        if (l != null && l.psi.isValid()) {
            return l.psi;
        }
        String text;
        try (InputStream in = PipoBuiltins.class.getResourceAsStream(resource)) {
            if (in == null) {
                return null;
            }
            text = new String(in.readAllBytes(), StandardCharsets.UTF_8).replace("\r\n", "\n");
        } catch (IOException e) {
            LOG.warn("cannot read " + resource, e);
            return null;
        }
        LightVirtualFile vf = new LightVirtualFile(fileName + ".pp", PipoFileType.INSTANCE, text);
        vf.setWritable(false);
        PsiFile psi = PsiManager.getInstance(project).findFile(vf);
        if (psi == null) {
            return null;
        }
        psi.putUserData(PipoKeys.STUB, stubName);
        files.put(fileName, new Loaded(vf, psi));
        return psi;
    }
}
