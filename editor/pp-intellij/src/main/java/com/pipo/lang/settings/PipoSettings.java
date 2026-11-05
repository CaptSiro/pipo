package com.pipo.lang.settings;

import com.intellij.execution.configurations.PathEnvironmentVariableUtil;
import com.intellij.openapi.components.PersistentStateComponent;
import com.intellij.openapi.components.Service;
import com.intellij.openapi.components.State;
import com.intellij.openapi.components.Storage;
import com.intellij.openapi.project.Project;
import com.intellij.openapi.util.SystemInfo;
import org.jetbrains.annotations.NotNull;
import org.jetbrains.annotations.Nullable;

import java.io.File;
import java.util.ArrayList;
import java.util.List;

/** Per-project settings: the interpreter and extra library directories. */
@Service(Service.Level.PROJECT)
@State(name = "PipoSettings", storages = @Storage("pipo.xml"))
public final class PipoSettings implements PersistentStateComponent<PipoSettings.Data> {
    public static final class Data {
        public String interpreterPath = "";
        public List<String> libraryPaths = new ArrayList<>();
    }

    private final Project project;
    private Data data = new Data();

    public PipoSettings(@NotNull Project project) {
        this.project = project;
    }

    public static PipoSettings getInstance(@NotNull Project project) {
        return project.getService(PipoSettings.class);
    }

    @Override
    public @NotNull Data getState() {
        return data;
    }

    @Override
    public void loadState(@NotNull Data state) {
        data = state;
    }

    public @NotNull String getInterpreterPath() {
        return data.interpreterPath == null ? "" : data.interpreterPath;
    }

    public void setInterpreterPath(@NotNull String path) {
        data.interpreterPath = path;
    }

    public @NotNull List<String> getLibraryPaths() {
        return data.libraryPaths == null ? List.of() : data.libraryPaths;
    }

    public void setLibraryPaths(@NotNull List<String> paths) {
        data.libraryPaths = new ArrayList<>(paths);
    }

    /** the configured interpreter, or an auto-detected one */
    public @Nullable String findInterpreter() {
        String explicit = getInterpreterPath().trim();
        if (!explicit.isEmpty()) {
            return explicit;
        }
        return autoDetectInterpreter();
    }

    /**
     * The newest pp executable in the project's build directories (build/,
     * cmake-build-*), or pp on the PATH.
     */
    public @Nullable String autoDetectInterpreter() {
        String exe = SystemInfo.isWindows ? "pp.exe" : "pp";
        String base = project.getBasePath();
        File best = null;
        if (base != null) {
            File root = new File(base);
            List<File> dirs = new ArrayList<>();
            dirs.add(new File(root, "build"));
            File[] children = root.listFiles();
            if (children != null) {
                for (File c : children) {
                    if (c.isDirectory() && c.getName().startsWith("cmake-build-")) {
                        dirs.add(c);
                    }
                }
            }
            for (File d : dirs) {
                File candidate = new File(d, exe);
                if (candidate.isFile() && (best == null || candidate.lastModified() > best.lastModified())) {
                    best = candidate;
                }
            }
        }
        if (best != null) {
            return best.getPath();
        }
        File onPath = PathEnvironmentVariableUtil.findInPath(exe);
        return onPath != null ? onPath.getPath() : null;
    }
}
