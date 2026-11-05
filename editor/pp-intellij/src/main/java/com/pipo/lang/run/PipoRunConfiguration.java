package com.pipo.lang.run;

import com.intellij.execution.ExecutionException;
import com.intellij.execution.Executor;
import com.intellij.execution.configurations.CommandLineState;
import com.intellij.execution.configurations.ConfigurationFactory;
import com.intellij.execution.configurations.GeneralCommandLine;
import com.intellij.execution.configurations.LocatableConfigurationBase;
import com.intellij.execution.configurations.RunConfiguration;
import com.intellij.execution.configurations.RunProfileState;
import com.intellij.execution.configurations.RuntimeConfigurationError;
import com.intellij.execution.configurations.RuntimeConfigurationException;
import com.intellij.execution.process.ProcessHandler;
import com.intellij.execution.process.ProcessHandlerFactory;
import com.intellij.execution.process.ProcessTerminatedListener;
import com.intellij.execution.runners.ExecutionEnvironment;
import com.intellij.openapi.options.SettingsEditor;
import com.intellij.openapi.project.Project;
import com.intellij.openapi.util.JDOMExternalizerUtil;
import com.intellij.util.execution.ParametersListUtil;
import com.pipo.lang.settings.PipoSettings;
import org.jdom.Element;
import org.jetbrains.annotations.NotNull;
import org.jetbrains.annotations.Nullable;

import java.io.File;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.List;

/** Runs `pp script.pp args...`. */
public final class PipoRunConfiguration extends LocatableConfigurationBase<RunProfileState> {
    private String scriptPath = "";
    private String scriptArgs = "";
    private String workingDir = "";
    private String interpreter = "";

    PipoRunConfiguration(Project project, ConfigurationFactory factory, String name) {
        super(project, factory, name);
    }

    public String getScriptPath() {
        return scriptPath;
    }

    public void setScriptPath(String scriptPath) {
        this.scriptPath = scriptPath == null ? "" : scriptPath;
    }

    public String getScriptArgs() {
        return scriptArgs;
    }

    public void setScriptArgs(String scriptArgs) {
        this.scriptArgs = scriptArgs == null ? "" : scriptArgs;
    }

    public String getWorkingDir() {
        return workingDir;
    }

    public void setWorkingDir(String workingDir) {
        this.workingDir = workingDir == null ? "" : workingDir;
    }

    /** an interpreter for this configuration only; empty means the project's */
    public String getInterpreter() {
        return interpreter;
    }

    public void setInterpreter(String interpreter) {
        this.interpreter = interpreter == null ? "" : interpreter;
    }

    @Nullable String resolveInterpreter() {
        if (!interpreter.isBlank()) {
            return interpreter.trim();
        }
        return PipoSettings.getInstance(getProject()).findInterpreter();
    }

    @Override
    public @NotNull SettingsEditor<? extends RunConfiguration> getConfigurationEditor() {
        return new PipoRunConfigurationEditor(getProject());
    }

    @Override
    public void checkConfiguration() throws RuntimeConfigurationException {
        if (scriptPath.isBlank()) {
            throw new RuntimeConfigurationError("No script selected");
        }
        if (!new File(scriptPath).isFile()) {
            throw new RuntimeConfigurationError("Script '" + scriptPath + "' does not exist");
        }
        String exe = resolveInterpreter();
        if (exe == null) {
            throw new RuntimeConfigurationError(
                    "The pp interpreter was not found: build the project, or set it in Settings | Languages & Frameworks | PiPo");
        }
        if (!new File(exe).isFile()) {
            throw new RuntimeConfigurationError("Interpreter '" + exe + "' does not exist");
        }
    }

    @Override
    public @Nullable String suggestedName() {
        return scriptPath.isBlank() ? null : new File(scriptPath).getName();
    }

    @Override
    public @Nullable RunProfileState getState(@NotNull Executor executor, @NotNull ExecutionEnvironment environment) {
        return new CommandLineState(environment) {
            @Override
            protected @NotNull ProcessHandler startProcess() throws ExecutionException {
                String exe = resolveInterpreter();
                if (exe == null) {
                    throw new ExecutionException("The pp interpreter was not found: build the project, "
                            + "or set it in Settings | Languages & Frameworks | PiPo");
                }
                GeneralCommandLine cmd = new GeneralCommandLine(exe)
                        .withParameters(scriptPath)
                        .withParameters(ParametersListUtil.parse(scriptArgs))
                        .withCharset(StandardCharsets.UTF_8)
                        .withParentEnvironmentType(GeneralCommandLine.ParentEnvironmentType.CONSOLE);
                String wd = workingDir.isBlank() ? new File(scriptPath).getParent() : workingDir;
                if (wd != null) {
                    cmd.withWorkDirectory(wd);
                }
                List<String> libs = new ArrayList<>(PipoSettings.getInstance(getProject()).getLibraryPaths());
                if (!libs.isEmpty()) {
                    String existing = System.getenv("PP_PATH");
                    if (existing != null && !existing.isBlank()) {
                        libs.add(existing);
                    }
                    cmd.withEnvironment("PP_PATH", String.join(File.pathSeparator, libs));
                }
                ProcessHandler handler = ProcessHandlerFactory.getInstance().createColoredProcessHandler(cmd);
                ProcessTerminatedListener.attach(handler);
                return handler;
            }
        };
    }

    @Override
    public void readExternal(@NotNull Element element) {
        super.readExternal(element);
        scriptPath = read(element, "SCRIPT_PATH");
        scriptArgs = read(element, "SCRIPT_ARGS");
        workingDir = read(element, "WORKING_DIR");
        interpreter = read(element, "INTERPRETER");
    }

    private static String read(Element element, String name) {
        String v = JDOMExternalizerUtil.readField(element, name);
        return v == null ? "" : v;
    }

    @Override
    public void writeExternal(@NotNull Element element) {
        super.writeExternal(element);
        JDOMExternalizerUtil.writeField(element, "SCRIPT_PATH", scriptPath);
        JDOMExternalizerUtil.writeField(element, "SCRIPT_ARGS", scriptArgs);
        JDOMExternalizerUtil.writeField(element, "WORKING_DIR", workingDir);
        JDOMExternalizerUtil.writeField(element, "INTERPRETER", interpreter);
    }
}
