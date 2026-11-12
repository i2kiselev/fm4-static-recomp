// Copyright (c) 2026, i2kiselev
// BSD 3-Clause License: see LICENSE in the repository root.
namespace Fm4StaticRecomp.Launcher.Core.Install;

public sealed record InstallLayout(string Root)
{
    public static InstallLayout ForLauncher(string launcherDirectory) =>
        new(Path.Combine(DataDirectory(launcherDirectory), "data"));

    // Keep mutable data outside the signed app bundle. Finder starts in an unrelated cwd.
    public static string DataDirectory(string launcherDirectory)
    {
        var directory = new DirectoryInfo(Path.GetFullPath(launcherDirectory));
        if (directory.Name == "MacOS" && directory.Parent?.Name == "Contents" &&
            directory.Parent.Parent is { } app && app.Name.EndsWith(".app", StringComparison.OrdinalIgnoreCase))
            return app.Parent!.FullName;
        return directory.FullName;
    }

    public string Game => Path.Combine(Root, "game");
    public string Content => Path.Combine(Root, "content");
    public string Toolchain => Path.Combine(Root, "toolchain");
    public string Update => Path.Combine(Root, "update");
    public string Logs => Path.Combine(Root, "logs");
    public string RuntimeLog => Path.Combine(Logs, "runtime.log");
    public string LauncherLog => Path.Combine(Logs, "launcher.log");
    public string Downloads => Path.Combine(Root, "downloads");
    public string Config => Path.Combine(Root, "config");
    public string GraphicsToml => Path.Combine(Config, "graphics.toml");
    public string ControlsToml => Path.Combine(Config, "controls.toml");
    public string WheelToml => Path.Combine(Config, "wheel.toml");
    public string LauncherToml => Path.Combine(Config, "launcher.toml");
    public string ConfigBackups => Path.Combine(Config, "backups");
    // Profiles, their backups and deleted profiles: data\ unless SETTINGS → SAVES FOLDER moved them (#20).
    public string SavesRoot { get; init; } = Root;
    public string Backups => Path.Combine(SavesRoot, "backups");
    public string Profiles => Path.Combine(SavesRoot, "profiles");
    public string Deleted => Path.Combine(SavesRoot, "deleted");
    public string SetupState => Path.Combine(Root, "setup-state.json");
    public string ContentInstallUser => Path.Combine(Root, "content-install", "user");
    public string Reports => Path.Combine(Root, "reports");
    public string Pfp4 => Path.Combine(Root, "pfp4");
    public string Pfp4Modes => Path.Combine(Pfp4, "modes");
    public string Pfp4SkipIntro(string mode, string save) => Path.Combine(Pfp4Modes, mode, "skip-intro", save);

    public string Source(string version) => Path.Combine(Root, "source", version);
    public string Cache(string version) => Path.Combine(Root, "cache", version);
    public string UiFonts(string version) => Path.Combine(Cache(version), "ui-fonts");
    // Native build outputs are kept per version, beside its extracted source.
    public string BuildFolder(string version) => Path.Combine(Source(version), "out", "build", OperatingSystem.IsMacOS() ? "mac-arm64-release" : "win-amd64-release");
    public string GameExecutable(string version) => Path.Combine(BuildFolder(version), OperatingSystem.IsMacOS() ? "fm4_static_recomp" : "fm4_static_recomp.exe");
    public string GraphicsSchema(string version) => Path.Combine(BuildFolder(version), "graphics-settings.json");
    public string SetupScript(string version) => Path.Combine(Source(version), "tools", OperatingSystem.IsMacOS() ? "setup-macos.py" : "setup.ps1");
    public string LicencesScript(string version) => Path.Combine(Source(version), "tools", "microsoft-licences.ps1");
    public string CrashReportScript(string version) => Path.Combine(Source(version), "tools", "create-crash-report.ps1");
    public string SetupLogs(string version) => Path.Combine(Source(version), ".local", "logs");
    public string SetupFailureRecord(string version) => Path.Combine(SetupLogs(version), "setup-error.json");
    public string DiscManifestOf(string version) => Path.Combine(Source(version), "config", "supported-discs.json");
    public string Pfp4ManifestOf(string version) => Path.Combine(Source(version), "config", "pfp4-1.3.json");
}
