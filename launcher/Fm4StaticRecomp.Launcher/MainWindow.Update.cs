// Portions copyright (c) 2026, Pinyon Shift contributors (BSD 3-Clause: see THIRD_PARTY_NOTICES.md).
// Copyright (c) 2026, i2kiselev (BSD 3-Clause: see LICENSE in the repository root).
// Adapted from Pinyon Shift's launcher (launcher/PinyonShift.Launcher.Desktop, BSD-3-Clause; see THIRD_PARTY_NOTICES.md).
using System.Diagnostics;
using Avalonia.Controls.ApplicationLifetimes;
using Avalonia.Interactivity;
using Fm4StaticRecomp.Launcher.Controls;
using Fm4StaticRecomp.Launcher.Core.Discs;
using Fm4StaticRecomp.Launcher.Core.Profiles;
using Fm4StaticRecomp.Launcher.Core.Setup;
using Fm4StaticRecomp.Launcher.Core.Updates;

namespace Fm4StaticRecomp.Launcher;

// Updates from inside the launcher (spec 13). The first click on UPDATE TO <version> explains
// what happens; the second downloads the release, checks its sums, swaps it in and restarts.
// The new version then builds beside the active one, which stays playable.
public partial class MainWindow
{
    private sealed record VersionOffer(string Label, string Note, Func<Task> Action);

    private ReleaseInfo? _update;
    private bool _updateConfirming;
    private bool _versionBuilding;
    // What the payload version's build needs next (RETRY, CHOOSE DISC 1); Ready shows it again
    // after every mode change.
    private VersionOffer? _versionOffer;

    private const string CloseTheGame = "Close the game first.";
    private static readonly string[] BlockedNotes =
        [CloseTheGame, "Available once the new version is built.", "Available once the current work is done."];

    private string? UpdateBlockedNote() =>
        _host is { GameRunning: true } ? BlockedNotes[0] : _versionBuilding ? BlockedNotes[1] : _busy ? BlockedNotes[2] : null;

    private static UpdateCoordinator Coordinator(LauncherHost host) =>
        new(new ReleaseClient(host.Http, host.Payload.OwnerRepo, ReleaseClient.ApiBase()), new Downloader(host.Http), host.Layout,
            System.Version.Parse(host.Payload.Version));

    private async Task StartUpdateCheckAsync(bool clicked = false)
    {
        if (_host is not { } host) return;
        // Windows releases contain an .exe and use Windows self replacement.
        // Until Mac bundles are published, keep updates manual.
        if (OperatingSystem.IsMacOS())
        {
            UpdateNote.Text = "macOS preview: download updates manually and keep the data folder.";
            return;
        }
        UpdateNote.Text = "";
        try
        {
            _update = await Coordinator(host).CheckAsync(CancellationToken.None);
        }
        catch (Exception error) when (error is HttpRequestException or InvalidDataException)
        {
            AppendLog($"Could not check for updates: {error.Message}");
            UpdateNote.Text = "Could not check for updates.";
            return;
        }
        if (_update is not { } update)
        {
            if (clicked) StatusText.Text = $"Up to date ({host.Payload.Version}).";
            return;
        }
        _updateConfirming = false;
        UpdateButton.Content = $"UPDATE TO {update.Version.ToString(3)}";
        UpdateButton.IsVisible = true;
        var notes = update.Notes.Split('\n', StringSplitOptions.RemoveEmptyEntries | StringSplitOptions.TrimEntries).FirstOrDefault() ?? "";
        StatusText.Text = $"{Branding.ProductName} {update.Version.ToString(3)}{(update.Prerelease ? " (pre-release)" : "")} is available. {notes}".Trim();
        AppendLog($"Update available: {update.Tag}{(update.Prerelease ? " (pre-release)" : "")}.");
        UpdateActions();
    }

    private void UpdateUpdateActions()
    {
        var note = UpdateBlockedNote();
        var blocked = note is not null;
        UpdateButton.IsEnabled = !blocked;
        if (UpdateButton.IsVisible && blocked) UpdateNote.Text = note;
        else if (BlockedNotes.Contains(UpdateNote.Text)) UpdateNote.Text = "";
        GoBackButton.IsVisible = _host is { } host && host.Versions.CanGoBack && !_versionBuilding && _mode == Mode.Ready;
        GoBackButton.IsEnabled = !blocked;
        if (_host?.Settings.PreviousVersion is { } previous) GoBackButton.Content = $"GO BACK TO {previous}";
        CheckUpdatesButton.IsEnabled = !_busy;
    }

    private async void CheckUpdates_Click(object? sender, RoutedEventArgs e) => await StartUpdateCheckAsync(clicked: true);

    private async void Update_Click(object? sender, RoutedEventArgs e)
    {
        if (_host is not { } host || _update is not { } update) return;
        if (UpdateBlockedNote() is { } blocked)
        {
            StatusText.Text = blocked;
            return;
        }
        var version = update.Version.ToString(3);
        if (!_updateConfirming)
        {
            _updateConfirming = true;
            StatusText.Text = $"{version} downloads from GitHub and the launcher restarts. Your saves, content and settings stay; " +
                              "the new version then builds beside the current one, which stays playable.";
            UpdateButton.Content = "DOWNLOAD AND RESTART";
            return;
        }
        StagedUpdate? staged = null;
        await RunWorkAsync(async cancellation =>
        {
            try
            {
                var progress = new Progress<double>(p => StatusText.Text = $"Downloading {version}: {p:P0}");
                staged = await Coordinator(host).DownloadAsync(update, progress, cancellation);
            }
            catch (Exception error) when (error is InvalidDataException or HttpRequestException or IOException or OperationCanceledException)
            {
                AppendLog($"Update failed: {error.Message}");
                StatusText.Text = error is ChecksumMismatchException
                    ? "The download did not match its checksum and was deleted; this launcher is unchanged."
                    : $"The update did not finish; this launcher is unchanged. {error.Message}";
                UpdateButton.Content = $"UPDATE TO {version}";
                _updateConfirming = false;
            }
        });
        if (staged is null || _closing) return;
        try
        {
            StatusText.Text = "Installing the new launcher.";
            UpdateCoordinator.Install(staged, host.LauncherExecutable, host.LauncherDirectory);
            try
            {
                Process.Start(new ProcessStartInfo(host.LauncherExecutable)
                {
                    UseShellExecute = false, WorkingDirectory = host.LauncherDirectory, ArgumentList = { SelfReplace.AfterUpdateSwitch },
                });
            }
            catch (Exception)
            {
                UpdateCoordinator.Undo(host.LauncherExecutable, host.LauncherDirectory);
                throw;
            }
            AppendLog($"Updated to {version}; restarting.");
            (Avalonia.Application.Current?.ApplicationLifetime as IClassicDesktopStyleApplicationLifetime)?.Shutdown();
        }
        catch (Exception error)
        {
            AppendLog($"Update failed: {error.Message}");
            StatusText.Text = $"The update did not finish; this launcher is unchanged. {error.Message}";
            UpdateButton.Content = $"UPDATE TO {version}";
            _updateConfirming = false;
        }
    }

    // A payload newer than every built version (the launcher was just updated): it builds
    // beside the active version, which PLAY keeps starting until this one is built.
    private async Task BuildPayloadVersionAsync()
    {
        if (_host is not { } host || host.Settings.ActiveVersion is not { } active || host.Setup.Disc1Release is not { } release) return;
        var version = host.Payload.Version;
        _versionOffer = null;
        if (_mode == Mode.Ready) AltButton.IsVisible = false;
        _versionBuilding = true;
        UpdateActions();
        using var cancel = new CancellationTokenSource();
        _cancel = cancel;
        try
        {
            _work = BuildSideBySideAsync(host, active, release, version, cancel.Token);
            await _work;
        }
        finally
        {
            _cancel = null;
            _work = null;
            _versionBuilding = false;
            UpdateActions();
        }
    }

    private async Task BuildSideBySideAsync(LauncherHost host, string active, string release, string version, CancellationToken cancellation)
    {
        try
        {
            SetProgress(-1, $"Building {version}: preparing the source.");
            await host.PrepareSourceAsync(cancellation);
            var next = await Task.Run(host.Manifest, cancellation);
            var installed = await Task.Run(() => DiscManifest.Load(host.Layout.DiscManifestOf(active)), cancellation);
            var needsDisc = await Task.Run(() => GameFolderCheck.NeedsExtraction(installed, next, release) ||
                                                 !GameFolderCheck.ExecutableMatches(host.Layout.Game, next, release,
                                                     host.Setup.Game == ProfileStore.Pfp4), cancellation);
            if (needsDisc)
            {
                // Spec 13: the active version stays playable until the player chooses the disc.
                ProgressPanel.IsVisible = false;
                Offer("CHOOSE DISC 1",
                    $"{version} needs files your installed Disc 1 lacks. You can keep playing {active}; CHOOSE DISC 1 extracts the disc again.",
                    ChooseDisc1Again);
                return;
            }
            // The new version's pinned files may come under new licences: asked again then (spec 5).
            if (await AskLicenceAsync(host, cancellation) is not { } licenceAccepted)
            {
                ProgressPanel.IsVisible = false;
                Offer("RETRY", $"Building {version} needs Microsoft's licence or their installer. You can keep playing {active}.",
                    BuildPayloadVersionAsync);
                return;
            }
            var request = new SetupRequest(version, host.Settings.BuildJobs, licenceAccepted, host.Settings.UseVisualStudioInstaller);
            var progress = new Progress<SetupEvent>(e =>
            {
                var overall = SetupProgress.Overall(e);
                SetProgress(overall, $"Building {version}: {e.Message}");
                AppendLog($"[{e.Stage} {e.Percent}%] {e.Message}");
            });
            var failure = await new VersionBuilder(host.Layout, host.Versions, host.Setup, host.Runner)
                .BuildVersionAsync(request, host.Payload.ZipPath, progress, AppendLog, cancellation);
            ProgressPanel.IsVisible = _mode != Mode.Ready;
            if (failure is not null)
            {
                AppendLog(FailureText(failure));
                Offer("RETRY", $"Building {version} failed: {failure.Message} You can keep playing {active}.", BuildPayloadVersionAsync);
                return;
            }
            host.ReloadFonts();
            VectorHeading.Fonts = host.Fonts;
            StatusText.Text = $"Updated to {version}.";
            if (_mode == Mode.Ready)
            {
                AltButton.IsVisible = false;
                UpdateSummary();
            }
        }
        catch (OperationCanceledException)
        {
            ProgressPanel.IsVisible = false;
        }
        catch (Exception error) when (error is IOException or UnauthorizedAccessException or InvalidDataException or InvalidOperationException)
        {
            ProgressPanel.IsVisible = false;
            StatusText.Text = $"Building {version} failed: {error.Message} You can keep playing {active}.";
        }
    }

    private void Offer(string label, string note, Func<Task> action)
    {
        _versionOffer = new VersionOffer(label, note, action);
        StatusText.Foreground = Brush("AccentBrush");
        StatusText.Text = note;
        if (_mode == Mode.Ready) ShowAltButton(label, action);
    }

    // Setup then extracts Disc 1 again and builds this version; until then the active version
    // stays playable, and a start without choosing checks again.
    private Task ChooseDisc1Again()
    {
        if (_host is not { GameRunning: false } host) return Task.CompletedTask;
        _versionOffer = null;
        host.Setup.Disc1Release = null;
        // A new Disc 1 extraction replaces the mod's files: setup puts the mod back after it.
        host.Setup.Pfp4Applied = false;
        ShowMode(Mode.Setup);
        Headline.Text = "CHOOSE DISC 1 AGAIN";
        Subhead.Text = "This version needs files your installed Disc 1 lacks.";
        StatusText.Text = "";
        return Task.CompletedTask;
    }

    private void GoBack_Click(object? sender, RoutedEventArgs e)
    {
        if (_host is not { GameRunning: false } host || !host.Versions.CanGoBack || _busy || _versionBuilding) return;
        host.Versions.GoBack();
        host.ReloadFonts();
        VectorHeading.Fonts = host.Fonts;
        AppendLog($"Went back to {host.Settings.ActiveVersion}.");
        StatusText.Text = $"Playing {host.Settings.ActiveVersion} again.";
        ShowMode(Mode.Ready);
    }
}
