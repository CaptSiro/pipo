package com.pipo.lang.settings;

import com.intellij.codeInsight.daemon.DaemonCodeAnalyzer;
import com.intellij.openapi.fileChooser.FileChooserDescriptorFactory;
import com.intellij.openapi.options.Configurable;
import com.intellij.openapi.project.Project;
import com.intellij.openapi.ui.TextFieldWithBrowseButton;
import com.intellij.psi.PsiManager;
import com.intellij.ui.components.JBLabel;
import com.intellij.ui.components.JBScrollPane;
import com.intellij.ui.components.JBTextArea;
import com.intellij.util.ui.FormBuilder;
import com.intellij.util.ui.UIUtil;
import org.jetbrains.annotations.Nls;
import org.jetbrains.annotations.NotNull;
import org.jetbrains.annotations.Nullable;

import javax.swing.JComponent;
import javax.swing.JPanel;
import java.util.ArrayList;
import java.util.List;
import java.util.Objects;

/** Settings | Languages & Frameworks | PiPo */
public final class PipoSettingsConfigurable implements Configurable {
    private final Project project;
    private TextFieldWithBrowseButton interpreter;
    private JBTextArea libraries;

    public PipoSettingsConfigurable(@NotNull Project project) {
        this.project = project;
    }

    @Override
    public @Nls String getDisplayName() {
        return "PiPo";
    }

    @Override
    public @Nullable JComponent createComponent() {
        interpreter = new TextFieldWithBrowseButton();
        interpreter.addBrowseFolderListener(project, FileChooserDescriptorFactory.createSingleFileNoJarsDescriptor()
                .withTitle("Select the pp Interpreter"));
        libraries = new JBTextArea(5, 40);
        String detected = PipoSettings.getInstance(project).autoDetectInterpreter();
        JBLabel hint = new JBLabel("Empty: auto-detect (newest pp in build/ or cmake-build-*/, then PATH). Now: "
                + (detected != null ? detected : "not found"));
        hint.setFontColor(UIUtil.FontColor.BRIGHTER);
        JBLabel libHint = new JBLabel("<html>Searched for <code>import name</code> after the importing file's folder, "
                + "before the project's lib/, the interpreter's lib/ and PP_PATH. Also passed to scripts as PP_PATH.</html>");
        libHint.setFontColor(UIUtil.FontColor.BRIGHTER);
        return FormBuilder.createFormBuilder()
                .addLabeledComponent("Interpreter (pp):", interpreter)
                .addComponentToRightColumn(hint)
                .addLabeledComponent("Library directories (one per line):", new JBScrollPane(libraries), true)
                .addComponentToRightColumn(libHint)
                .addComponentFillVertically(new JPanel(), 0)
                .getPanel();
    }

    private List<String> libraryLines() {
        List<String> out = new ArrayList<>();
        for (String l : libraries.getText().split("\\R")) {
            if (!l.isBlank()) {
                out.add(l.trim());
            }
        }
        return out;
    }

    @Override
    public boolean isModified() {
        PipoSettings s = PipoSettings.getInstance(project);
        return !Objects.equals(interpreter.getText().trim(), s.getInterpreterPath())
                || !libraryLines().equals(s.getLibraryPaths());
    }

    @Override
    public void apply() {
        PipoSettings s = PipoSettings.getInstance(project);
        s.setInterpreterPath(interpreter.getText().trim());
        s.setLibraryPaths(libraryLines());
        // imports may resolve differently now
        PsiManager.getInstance(project).dropPsiCaches();
        DaemonCodeAnalyzer.getInstance(project).restart();
    }

    @Override
    public void reset() {
        PipoSettings s = PipoSettings.getInstance(project);
        interpreter.setText(s.getInterpreterPath());
        libraries.setText(String.join("\n", s.getLibraryPaths()));
    }
}
