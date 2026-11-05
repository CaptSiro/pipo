package com.pipo.lang.run;

import com.intellij.execution.actions.ConfigurationContext;
import com.intellij.execution.actions.LazyRunConfigurationProducer;
import com.intellij.execution.configurations.ConfigurationFactory;
import com.intellij.execution.configurations.ConfigurationTypeBase;
import com.intellij.execution.configurations.ConfigurationTypeUtil;
import com.intellij.execution.configurations.RunConfiguration;
import com.intellij.execution.filters.ConsoleFilterProvider;
import com.intellij.execution.filters.Filter;
import com.intellij.execution.filters.OpenFileHyperlinkInfo;
import com.intellij.execution.lineMarker.RunLineMarkerContributor;
import com.intellij.icons.AllIcons;
import com.intellij.openapi.project.Project;
import com.intellij.openapi.util.Ref;
import com.intellij.openapi.util.io.FileUtil;
import com.intellij.openapi.vfs.LocalFileSystem;
import com.intellij.openapi.vfs.VirtualFile;
import com.intellij.psi.PsiElement;
import com.intellij.psi.PsiFile;
import com.pipo.lang.PipoIcons;
import com.pipo.lang.parser.PipoElementTypes;
import com.pipo.lang.psi.PipoFile;
import com.pipo.lang.psi.PipoPsiUtil;
import org.jetbrains.annotations.NotNull;
import org.jetbrains.annotations.Nullable;

import java.io.File;
import java.util.ArrayList;
import java.util.List;
import java.util.regex.Matcher;
import java.util.regex.Pattern;

/** The run configuration type, its producer, the gutter run icon and the console links. */
public final class PipoRunSupport {
    private PipoRunSupport() {
    }

    public static final class Type extends ConfigurationTypeBase {
        public static final String ID = "PipoRunConfiguration";

        public Type() {
            super(ID, "PiPo", "Run a PiPo script with the pp interpreter", PipoIcons.FILE);
            addFactory(new ConfigurationFactory(this) {
                @Override
                public @NotNull String getId() {
                    return ID;
                }

                @Override
                public @NotNull RunConfiguration createTemplateConfiguration(@NotNull Project project) {
                    return new PipoRunConfiguration(project, this, "PiPo");
                }
            });
        }

        static Type getInstance() {
            return ConfigurationTypeUtil.findConfigurationType(Type.class);
        }
    }

    private static @Nullable VirtualFile scriptOf(@Nullable PsiElement location) {
        if (location == null) {
            return null;
        }
        PsiFile file = location instanceof PsiFile f ? f : location.getContainingFile();
        if (!(file instanceof PipoFile)) {
            return null;
        }
        VirtualFile vf = file.getVirtualFile();
        return vf != null && vf.isInLocalFileSystem() ? vf : null;
    }

    /** right click | Run 'file.pp', and Ctrl+Shift+F10 */
    public static final class Producer extends LazyRunConfigurationProducer<PipoRunConfiguration> {
        @Override
        public @NotNull ConfigurationFactory getConfigurationFactory() {
            return Type.getInstance().getConfigurationFactories()[0];
        }

        @Override
        protected boolean setupConfigurationFromContext(@NotNull PipoRunConfiguration configuration,
                                                        @NotNull ConfigurationContext context,
                                                        @NotNull Ref<PsiElement> sourceElement) {
            VirtualFile vf = scriptOf(context.getPsiLocation());
            if (vf == null) {
                return false;
            }
            configuration.setScriptPath(vf.getPath());
            if (vf.getParent() != null) {
                configuration.setWorkingDir(vf.getParent().getPath());
            }
            configuration.setName(vf.getName());
            return true;
        }

        @Override
        public boolean isConfigurationFromContext(@NotNull PipoRunConfiguration configuration,
                                                  @NotNull ConfigurationContext context) {
            VirtualFile vf = scriptOf(context.getPsiLocation());
            return vf != null && FileUtil.pathsEqual(configuration.getScriptPath(), vf.getPath());
        }
    }

    /**
     * A run icon on the first line of a script. Files that export something are
     * modules rather than scripts, so they get none.
     */
    public static final class LineMarker extends RunLineMarkerContributor {
        @Override
        public @Nullable Info getInfo(@NotNull PsiElement element) {
            if (element.getFirstChild() != null || !(element.getContainingFile() instanceof PipoFile file)) {
                return null;
            }
            PsiElement first = null;
            for (PsiElement s : PipoPsiUtil.statements(file)) {
                if (PipoPsiUtil.is(s, PipoElementTypes.EXPORT_STMT)
                        || (s instanceof com.pipo.lang.psi.PipoImportStmt i && i.isReexport())) {
                    return null;
                }
                if (first == null) {
                    first = s;
                }
            }
            if (first == null || element.getTextRange().getStartOffset() != first.getTextRange().getStartOffset()) {
                return null;
            }
            return withExecutorActions(AllIcons.RunConfigurations.TestState.Run);
        }
    }

    /** makes "path/file.pp:12:5: error" in any console a link to that line */
    public static final class ConsoleFilters implements ConsoleFilterProvider {
        @Override
        public Filter @NotNull [] getDefaultFilters(@NotNull Project project) {
            return new Filter[]{new ErrorLocationFilter(project)};
        }
    }

    static final class ErrorLocationFilter implements Filter {
        private static final Pattern LOCATION = Pattern.compile(
                "([A-Za-z]:[\\\\/][^:\\r\\n]*?\\.pp|[^\\s:'\"(]+\\.pp):(\\d+)(?::(\\d+))?");
        private final Project project;

        ErrorLocationFilter(Project project) {
            this.project = project;
        }

        @Override
        public @Nullable Result applyFilter(@NotNull String line, int entireLength) {
            int lineStart = entireLength - line.length();
            List<ResultItem> items = new ArrayList<>();
            Matcher m = LOCATION.matcher(line);
            while (m.find()) {
                VirtualFile vf = find(m.group(1));
                if (vf == null) {
                    continue;
                }
                int lineNo = Integer.parseInt(m.group(2)) - 1;
                int col = m.group(3) != null ? Integer.parseInt(m.group(3)) - 1 : 0;
                items.add(new ResultItem(lineStart + m.start(), lineStart + m.end(),
                        new OpenFileHyperlinkInfo(project, vf, Math.max(lineNo, 0), Math.max(col, 0))));
            }
            return items.isEmpty() ? null : new Result(items);
        }

        private @Nullable VirtualFile find(String path) {
            File f = new File(path);
            if (!f.isAbsolute() && project.getBasePath() != null) {
                f = new File(project.getBasePath(), path);
            }
            return f.isFile() ? LocalFileSystem.getInstance().findFileByIoFile(f) : null;
        }
    }
}
