// Portions copyright (c) 2026, Pinyon Shift contributors (BSD 3-Clause: see THIRD_PARTY_NOTICES.md).
// Copyright (c) 2026, i2kiselev (BSD 3-Clause: see LICENSE in the repository root).
// Adapted from Pinyon Shift's launcher (launcher/PinyonShift.Launcher.Desktop, BSD-3-Clause; see THIRD_PARTY_NOTICES.md).
using System.Diagnostics;
using Fm4StaticRecomp.Launcher.Core.Content;
using Fm4StaticRecomp.Launcher.Core.Discs;
using Fm4StaticRecomp.Launcher.Core.Fonts;
using Fm4StaticRecomp.Launcher.Core.Install;
using Fm4StaticRecomp.Launcher.Core.Pfp4;
using Fm4StaticRecomp.Launcher.Core.Platform;
using Fm4StaticRecomp.Launcher.Core.Profiles;
using Fm4StaticRecomp.Launcher.Core.Settings;
using Fm4StaticRecomp.Launcher.Core.Setup;
using Fm4StaticRecomp.Launcher.Core.Updates;

namespace Fm4StaticRecomp.Launcher;

public sealed class LauncherStartException(string headline, string message) : Exception(message)
{
    public string Headline { get; } = headline;
}

// Everything the window drives, over the core library: Pinyon's Installation, which drove
// tools/pinyon.py, replaced.
public sealed class LauncherHost
{
    private bool _gameRunning;

    private readonly InstallLock _lock;

    private LauncherHost(string launcherDirectory, InstallLayout layout, PayloadInfo payload, InstallLock installLock)
    {
        _lock = installLock;
        LauncherDirectory = launcherDirectory;
        Payload = payload;
        Platform = Platforms.Current;
        Settings = LauncherSettings.Load(layout.LauncherToml);
        layout = layout with { SavesRoot = Settings.SavesFolder ?? layout.Root };
        Layout = layout;
        Setup = SetupState.Load(layout.SetupState);
        Profiles = new ProfileStore(layout, Platform, Setup.Game ?? "vanilla");
        Versions = new VersionStore(layout, Settings);
        Content = new ContentInstaller(layout, Platform);
        Runner = new SetupRunner(layout, Platform);
        ReloadFonts();
    }

    public string LauncherDirectory { get; }
    public string LauncherExecutable => Environment.ProcessPath ?? Path.Combine(LauncherDirectory, ReleaseClient.LauncherAsset);
    public InstallLayout Layout { get; private set; }
    public IPlatform Platform { get; }
    public PayloadInfo Payload { get; }
    public LauncherSettings Settings { get; }
    public SetupState Setup { get; }
    public ProfileStore Profiles { get; private set; }
    public VersionStore Versions { get; }
    public ContentInstaller Content { get; }
    public SetupRunner Runner { get; }
    public HttpClient Http { get; } = new() { Timeout = Timeout.InfiniteTimeSpan };
    public UiFonts? Fonts { get; private set; }

    public bool GameRunning
    {
        get => _gameRunning;
        set
        {
            if (_gameRunning == value) return;
            _gameRunning = value;
            Changed?.Invoke();
        }
    }

    public event Action? Changed;

    // lockWait: how long to wait for another launcher on this folder to exit (an update's restart).
    public static Task<LauncherHost> OpenAsync(string launcherDirectory, string launcherVersion, TimeSpan lockWait, string? dataRoot = null) => Task.Run(() =>
    {
        var layout = dataRoot is null ? InstallLayout.ForLauncher(launcherDirectory) : new InstallLayout(dataRoot);
        if (RootCheck.Problem(layout.Root, RootEnvironment.Current) is { } problem)
            throw new LauncherStartException(OperatingSystem.IsMacOS() ? "GAME DATA FOLDER UNAVAILABLE" : "MOVE THE LAUNCHER'S FOLDER", problem);
        var installLock = InstallLock.TryAcquire(layout.Root, lockWait)
                          ?? throw new LauncherStartException("ALREADY RUNNING",
                              "Another FM4 Static Recomp launcher is using this folder. Switch to its window, or close it and start this one again.");
        PayloadInfo payload;
        try
        {
            payload = PayloadInfo.Read(launcherDirectory, launcherVersion);
        }
        catch (Exception ex) when (ex is IOException or InvalidDataException or UnauthorizedAccessException or
                                       System.Text.Json.JsonException or KeyNotFoundException)
        {
            installLock.Dispose();
            throw new LauncherStartException("SOMETHING IS MISSING", ex.Message);
        }
        try
        {
            return new LauncherHost(launcherDirectory, layout, payload, installLock);
        }
        catch (Exception ex) when (ex is System.Text.Json.JsonException or IOException or UnauthorizedAccessException)
        {
            installLock.Dispose();
            throw new LauncherStartException("THE INSTALL CANNOT BE READ",
                $"{ex.Message} If {layout.SetupState} is damaged, delete it: setup then runs again, and your saves, " +
                "content and settings stay.");
        }
    });

    public void ReloadFonts() =>
        Fonts = Settings.ActiveVersion is { } version ? UiFonts.Load(Layout.UiFonts(version)) : null;

    public Task PrepareSourceAsync(CancellationToken cancellation) =>
        Versions.ExtractPayloadAsync(Payload.ZipPath, Payload.Version, cancellation);

    public DiscManifest Manifest() => DiscManifest.Load(Layout.DiscManifestOf(Payload.Version));

    public Pfp4Manifest ModManifest(string version) => Pfp4Manifest.Load(Layout.Pfp4ManifestOf(version));

    // Fixed at setup's Disc 1 step; the store stamps every new profile with the install's game.
    public void RecordGame(string? game)
    {
        Setup.Game = game;
        Profiles = new ProfileStore(Layout, Platform, game ?? "vanilla");
    }

    public bool SavesFolderMissing => Settings.SavesFolder is { } folder && !Directory.Exists(folder);

    // Null folder: back to the data folder. otherGameNote: the player went past the warning on the
    // other game's saves in the folder. Returns why the old saves could not be removed, if they could not.
    // Called on the UI thread: only the file work runs aside, settings and layout change here, where
    // every other settings save happens.
    public async Task<string?> MoveSavesAsync(string? folder, string? otherGameNote)
    {
        var old = Layout;
        var target = old with { SavesRoot = folder is null ? old.Root : Path.TrimEndingDirectorySeparator(Path.GetFullPath(folder)) };
        if (target.SavesRoot.Equals(old.SavesRoot, StringComparison.OrdinalIgnoreCase)) return null;
        var game = Setup.Game ?? "vanilla";
        var move = await Task.Run(() =>
        {
            if (SavesFolder.Problem(old, target.SavesRoot) is { } problem) throw new IOException(problem);
            return SavesFolder.Copy(old, target, Platform);
        });
        var (savesFolder, activeProfile) = (Settings.SavesFolder, Settings.ActiveProfile);
        try
        {
            if (otherGameNote is not null) await Task.Run(() => SavesFolder.NoteOtherGame(target.SavesRoot, game, otherGameNote));
            Settings.SavesFolder = target.SavesRoot.Equals(old.Root, StringComparison.OrdinalIgnoreCase) ? null : target.SavesRoot;
            if (activeProfile is not null && move.Renamed.TryGetValue(activeProfile, out var renamed)) Settings.ActiveProfile = renamed;
            Settings.Save();
        }
        catch (Exception error) when (error is IOException or UnauthorizedAccessException)
        {
            (Settings.SavesFolder, Settings.ActiveProfile) = (savesFolder, activeProfile);
            await Task.Run(() => SavesFolder.Undo(move));
            throw;
        }
        Layout = target;
        Profiles = new ProfileStore(Layout, Platform, game);
        try
        {
            await Task.Run(() => SavesFolder.Remove(move));
            return null;
        }
        catch (Exception error) when (error is IOException or UnauthorizedAccessException)
        {
            return $"The saves are in {target.SavesRoot}, but the old copies in {old.SavesRoot} could not be removed: {error.Message}";
        }
    }

    public GraphicsSettings? Graphics()
    {
        if (Settings.ActiveVersion is not { } version || !File.Exists(Layout.GraphicsSchema(version))) return null;
        return new GraphicsSettings(GraphicsSchema.Load(Layout.GraphicsSchema(version)), Layout.GraphicsToml, Layout.ConfigBackups);
    }

    // Null while no version is active or the active one predates controls.toml.
    public ControlsSettings? Controls() =>
        Settings.ActiveVersion is { } version && ControlsSettings.Supports(version) ? new ControlsSettings(Layout.ControlsToml) : null;

    public WheelSettings Wheel() => new(Layout.WheelToml);

    // The active build's executable when it has the WHEEL panel's live view, else null.
    public string? WheelProbeExecutable() =>
        Settings.ActiveVersion is { } version && WheelProbe.Supported(Layout.GameExecutable(version))
            ? Layout.GameExecutable(version) : null;

    public static void Open(string target)
    {
        try { Process.Start(new ProcessStartInfo(target) { UseShellExecute = true }); }
        catch (Exception) { }
    }

    // Shows a file selected in its folder.
    public static void Reveal(string file)
    {
        try
        {
            var start = OperatingSystem.IsMacOS()
                ? new ProcessStartInfo("/usr/bin/open") { ArgumentList = { "-R", file } }
                : new ProcessStartInfo("explorer.exe", $"/select,\"{file}\"") { UseShellExecute = true };
            Process.Start(start);
        }
        catch (Exception) { }
    }
}
