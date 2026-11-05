package com.pipo.lang.run;

import com.intellij.openapi.fileChooser.FileChooserDescriptorFactory;
import com.intellij.openapi.options.SettingsEditor;
import com.intellij.openapi.project.Project;
import com.intellij.openapi.ui.TextFieldWithBrowseButton;
import com.intellij.ui.RawCommandLineEditor;
import com.intellij.util.ui.FormBuilder;
import com.pipo.lang.settings.PipoSettings;
import org.jetbrains.annotations.NotNull;

import javax.swing.JComponent;
import javax.swing.JPanel;

final class PipoRunConfigurationEditor extends SettingsEditor<PipoRunConfiguration> {
    private final TextFieldWithBrowseButton script = new TextFieldWithBrowseButton();
    private final RawCommandLineEditor args = new RawCommandLineEditor();
    private final TextFieldWithBrowseButton workingDir = new TextFieldWithBrowseButton();
    private final TextFieldWithBrowseButton interpreter = new TextFieldWithBrowseButton();
    private final JPanel panel;

    PipoRunConfigurationEditor(Project project) {
        script.addBrowseFolderListener(project, FileChooserDescriptorFactory.createSingleFileDescriptor("pp")
                .withTitle("Select PiPo Script"));
        workingDir.addBrowseFolderListener(project, FileChooserDescriptorFactory.createSingleFolderDescriptor()
                .withTitle("Select Working Directory"));
        interpreter.addBrowseFolderListener(project, FileChooserDescriptorFactory.createSingleFileNoJarsDescriptor()
                .withTitle("Select the pp Interpreter"));
        String detected = PipoSettings.getInstance(project).findInterpreter();
        interpreter.getTextField().setToolTipText("Empty: the project's interpreter"
                + (detected != null ? " (" + detected + ")" : ""));

        panel = FormBuilder.createFormBuilder()
                .addLabeledComponent("Script:", script)
                .addLabeledComponent("Script arguments:", args)
                .addLabeledComponent("Working directory:", workingDir)
                .addTooltip("Empty: the script's directory")
                .addLabeledComponent("Interpreter:", interpreter)
                .addTooltip("Empty: the project's interpreter" + (detected != null ? " (" + detected + ")" : "")
                        + ". Settings | Languages & Frameworks | PiPo")
                .getPanel();
    }

    @Override
    protected void resetEditorFrom(@NotNull PipoRunConfiguration c) {
        script.setText(c.getScriptPath());
        args.setText(c.getScriptArgs());
        workingDir.setText(c.getWorkingDir());
        interpreter.setText(c.getInterpreter());
    }

    @Override
    protected void applyEditorTo(@NotNull PipoRunConfiguration c) {
        c.setScriptPath(script.getText().trim());
        c.setScriptArgs(args.getText());
        c.setWorkingDir(workingDir.getText().trim());
        c.setInterpreter(interpreter.getText().trim());
    }

    @Override
    protected @NotNull JComponent createEditor() {
        return panel;
    }
}
