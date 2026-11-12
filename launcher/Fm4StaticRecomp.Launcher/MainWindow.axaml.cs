// Portions copyright (c) 2026, Pinyon Shift contributors (BSD 3-Clause: see THIRD_PARTY_NOTICES.md).
// Copyright (c) 2026, i2kiselev (BSD 3-Clause: see LICENSE in the repository root).
// Adapted from Pinyon Shift's launcher (launcher/PinyonShift.Launcher.Desktop, BSD-3-Clause; see THIRD_PARTY_NOTICES.md).
using System.Collections.Concurrent;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Controls.ApplicationLifetimes;
using Avalonia.Input;
using Avalonia.Interactivity;
using Avalonia.Media;
using Avalonia.Platform.Storage;
using Avalonia.Threading;
using Fm4StaticRecomp.Launcher.Controls;
using Fm4StaticRecomp.Launcher.Core.Discs;
using Fm4StaticRecomp.Launcher.Core.Install;
using Fm4StaticRecomp.Launcher.Core.Updates;

namespace Fm4StaticRecomp.Launcher;

// The window over the core library (LauncherHost). The panels live in the MainWindow.*.cs files.
public partial class MainWindow : Window
{
    private enum Mode { Loading, Setup, Building, Ready, Playing, Crashed }

    public static readonly string Version = typeof(MainWindow).Assembly.GetName().Version?.ToString(3) ?? "0.0.0";

    private readonly string[] _args;
    // Set while --skip-launcher keeps the window closed; cleared once it opens.
    private SkipLauncherRequest? _skip;
    private LauncherHost? _host;
    private Mode _mode = Mode.Loading;
    private CancellationTokenSource? _cancel;
    // The setup or install work running now; closing the window waits for it to stop.
    private Task? _work;
    private bool _closing;
    // The log box shows the last lines only; the whole log is the session log file. Lines from
    // any thread are queued and shown in batches: a compile writes a hundred a second, and
    // re-shaping the box's text for each one starved the window.
    private const int LogBoxLines = 300;
    private readonly ConcurrentQueue<string> _pendingLog = new();
    private readonly Queue<string> _logTail = new();
    private readonly object _sessionLogGate = new();
    private StreamWriter? _sessionLog;
    private bool _busy;
    private MacDataLocation? _dataLocation;
    private string? _dataRoot;

    public MainWindow() : this([]) { }

    public MainWindow(string[] args)
    {
        _args = args;
        _skip = SkipLauncherRequest.Parse(args);
        InitializeComponent();
        Title = Branding.ProductName;
        ProductText.Text = Branding.ProductName.ToUpperInvariant();
        VersionText.Text = $"{Branding.ProductName} {Version} · {Branding.Disclaimer}";
        FitToScreen();
        WatchSettingsEdits();
        AddHandler(DragDrop.DropEvent, OnDrop);
        AddHandler(DragDrop.DragOverEvent, OnDragOver);
        AddHandler(DragDrop.DragLeaveEvent, (_, _) => DropOverlay.IsVisible = false);
        if (_skip is null) Opened += async (_, _) => await LoadAsync();
        else Dispatcher.UIThread.Post(async () => await LoadAsync());
        new DispatcherTimer(TimeSpan.FromMilliseconds(250), DispatcherPriority.Background, (_, _) => FlushLog()).Start();
        Dispatcher.UIThread.UnhandledException += OnUnhandledException;
        TaskScheduler.UnobservedTaskException += (_, e) =>
        {
            AppendLog($"UNEXPECTED ERROR in background work: {e.Exception}");
            e.SetObserved();
        };
    }

    // A window taller than the screen hid the bottom bar's buttons below the taskbar (a 1366x768
    // laptop at 125 % has about 1090x580 to show); the title bar and frame sit outside Height.
    private void FitToScreen()
    {
        const double frame = 40;
        if (Screens.Primary is not { } screen) return;
        var area = screen.WorkingArea.Size.ToSize(screen.Scaling);
        Width = Math.Min(Width, area.Width);
        Height = Math.Min(Height, area.Height - frame);
        MinWidth = Math.Min(MinWidth, Width);
        MinHeight = Math.Min(MinHeight, Height);
    }

    // The last chance: an error no handler expected is logged and shown instead of closing the
    // launcher without a word.
    private async void OnUnhandledException(object? sender, DispatcherUnhandledExceptionEventArgs e)
    {
        e.Handled = true;
        AppendLog($"UNEXPECTED ERROR: {e.Exception}");
        FlushLog();
        OpenSkippedWindow();
        var log = _host?.Layout.LauncherLog ?? "the launcher's log";
        await Dialog.AskAsync(this, "SOMETHING WENT WRONG",
            $"{e.Exception.Message}\n\nThe details are in {log}. REPORT A PROBLEM opens an issue; attach that file.", "OK");
    }

    private string Repository => _host?.Payload.Repository ?? Branding.Repository;

    private IBrush Brush(string key) => (IBrush)this.FindResource(key)!;

    private async Task LoadAsync()
    {
        var environment = RootEnvironment.Current;
        if (OperatingSystem.IsMacOS() && MacDataLocation.UsesApplicationSupport(AppContext.BaseDirectory, environment))
        {
            _dataLocation = MacDataLocation.Current;
            try
            {
                _dataRoot = _dataLocation.Read();
            }
            catch (Exception error) when (error is IOException or UnauthorizedAccessException)
            {
                ShowDataLocation(_dataLocation.DefaultRoot, error.Message);
                return;
            }
            if (_dataRoot is null && !File.Exists(new InstallLayout(_dataLocation.DefaultRoot).SetupState))
            {
                ShowDataLocation(_dataLocation.DefaultRoot);
                return;
            }
            _dataRoot ??= _dataLocation.DefaultRoot;
        }
        await OpenInstallAsync();
    }

    private void ShowDataLocation(string root, string? problem = null)
    {
        Headline.Text = "GAME DATA FOLDER";
        Subhead.Text = problem is null ? $"Game data will be installed in {root}." : $"{problem}\n\n{root}";
        SetupPanel.IsVisible = PrimaryButton.IsVisible = SavesButton.IsVisible = false;
        DataLocationPanel.IsVisible = true;
        OpenSkippedWindow();
    }

    private async void UseDataLocation_Click(object? sender, RoutedEventArgs e) =>
        await SelectDataLocationAsync(_dataRoot ?? _dataLocation!.DefaultRoot);

    private async void ChooseDataLocation_Click(object? sender, RoutedEventArgs e)
    {
        if (!_busy && await PickFolderAsync("Choose a dedicated folder for FM4 game data") is { } root)
            await SelectDataLocationAsync(root);
    }

    private async Task SelectDataLocationAsync(string root)
    {
        if (_busy) return;
        _busy = true;
        IsEnabled = false;
        try
        {
            root = Path.TrimEndingDirectorySeparator(Path.GetFullPath(root));
            var problem = await Task.Run(() => RootCheck.Problem(root, RootEnvironment.Current));
            if (problem is not null)
            {
                ShowDataLocation(_dataRoot ?? _dataLocation!.DefaultRoot, problem);
                return;
            }
            await Task.Run(() => _dataLocation!.Save(root));
            _dataRoot = root;
        }
        catch (Exception error) when (error is IOException or UnauthorizedAccessException)
        {
            ShowDataLocation(_dataRoot ?? _dataLocation!.DefaultRoot, error.Message);
            return;
        }
        finally
        {
            _busy = false;
            IsEnabled = true;
        }
        await OpenInstallAsync();
    }

    private async Task OpenInstallAsync()
    {
        _busy = true;
        IsEnabled = false;
        try
        {
            var lockWait = _args.Contains(SelfReplace.AfterUpdateSwitch) ? TimeSpan.FromSeconds(30) : TimeSpan.Zero;
            _host = await LauncherHost.OpenAsync(AppContext.BaseDirectory, Version, lockWait, _dataRoot);
        }
        catch (LauncherStartException error)
        {
            if (_dataLocation is not null && error.Headline == "GAME DATA FOLDER UNAVAILABLE")
            {
                ShowDataLocation(_dataRoot!, error.Message);
                return;
            }
            DataLocationPanel.IsVisible = false;
            Headline.Text = error.Headline;
            Subhead.Text = error.Message;
            SetupPanel.IsVisible = false;
            PrimaryButton.IsVisible = false;
            SavesButton.IsVisible = false;
            OpenSkippedWindow();
            return;
        }
        finally
        {
            _busy = false;
            IsEnabled = true;
        }
        DataLocationPanel.IsVisible = false;
        VectorHeading.Fonts = _host.Fonts;
        _host.Changed += () => Dispatcher.UIThread.Post(UpdateActions);
        StartSessionLog();
        AppendLog($"Release source: {_host.Layout.Source(_host.Payload.Version)}");
        AppendLog($"Install root: {_host.Layout.Root}");
        var host = _host;
        if (!OperatingSystem.IsMacOS() && _args.Contains(SelfReplace.AfterUpdateSwitch))
            _ = Task.Run(() => UpdateCoordinator.CleanUp(host.LauncherExecutable, host.LauncherDirectory,
                UpdateCoordinator.DownloadFolder(host.Layout, System.Version.Parse(Version))));
        try
        {
            // A replacement of the game folder cut short between its two renames.
            await Task.Run(() => Disc1Step.RecoverInterruptedReplace(host.Layout.Game));
        }
        catch (Exception error) when (error is IOException or UnauthorizedAccessException)
        {
            AppendLog($"Could not tidy the game folder: {error.Message}");
        }
        foreach (var line in await Task.Run(host.Profiles.RepairXeniaImports)) AppendLog(line);
        if (_skip is null) _ = StartUpdateCheckAsync();
        _ = RefreshFlagsAsync();
        var kind = StartDecision.Decide(host.Setup, host.Profiles.List().Count, host.Payload.Version, host.SavesFolderMissing);
        string? note = null;
        if (_skip is { } skip)
        {
            var start = skip.Decide(kind, host.Profiles.List(), host.Settings.ActiveProfile);
            if (start.Profile is { } profile)
            {
                ShowMode(Mode.Ready);
                await PlayAsync(profile);
                return;
            }
            note = start.Note;
            OpenSkippedWindow();
        }
        switch (kind)
        {
            case StartKind.Setup:
                ShowMode(Mode.Setup);
                break;
            case StartKind.BuildVersion:
                ShowMode(Mode.Ready);
                await BuildPayloadVersionAsync();
                break;
            default:
                ShowMode(Mode.Ready);
                if (note is not null) StatusText.Text = note;
                break;
        }
    }

    // --skip-launcher opens the window once the player has to act; from then on it runs as usual.
    private void OpenSkippedWindow()
    {
        if (_skip is null) return;
        _skip = null;
        ((IClassicDesktopStyleApplicationLifetime)Application.Current!.ApplicationLifetime!).MainWindow = this;
        Show();
        _ = StartUpdateCheckAsync();
    }

    // The game quit normally under --skip-launcher: the frontend gets control back.
    private void ExitSkippedLauncher()
    {
        FlushLog();
        lock (_sessionLogGate)
        {
            _sessionLog?.Dispose();
            _sessionLog = null;
        }
        ((IClassicDesktopStyleApplicationLifetime)Application.Current!.ApplicationLifetime!).Shutdown();
    }

    private void ShowMode(Mode mode)
    {
        _mode = mode;
        // The game takes the controllers and the force device itself.
        if (mode != Mode.Ready) StopWheelProbe();
        SetupPanel.IsVisible = mode is Mode.Setup or Mode.Building;
        PrimaryButton.IsVisible = true;
        AltButton.IsVisible = false;
        if (mode == Mode.Ready && _versionOffer is { } offer) ShowAltButton(offer.Label, offer.Action);
        NavPanel.IsVisible = mode == Mode.Ready;
        ProfileCombo.IsVisible = ProfileButton.IsVisible = mode is Mode.Ready or Mode.Playing;
        CrashPanel.IsVisible = mode == Mode.Crashed;
        ReportButton.IsVisible = mode != Mode.Crashed;
        LogsButton.Content = mode == Mode.Crashed ? "REPORT FOLDER" : "LOGS";
        if (mode != Mode.Ready) ShowPanel(Panel.None);
        CancelButton.IsVisible = mode == Mode.Building;
        if (mode == Mode.Building) ProgressPanel.IsVisible = true;
        StatusText.Foreground = Brush("ValueBrush");
        switch (mode)
        {
            case Mode.Setup:
                Headline.Text = "BUILD YOUR COPY";
                Subhead.Text = "Verified, extracted and compiled on this computer. Nothing is uploaded.";
                PrimaryButton.Content = "VERIFY AND BUILD";
                break;
            case Mode.Building:
                Headline.Text = "BUILDING";
                Subhead.Text = "Closing this window stops the build; the next start resumes it.";
                PrimaryButton.Content = "BUILDING…";
                break;
            case Mode.Ready:
                Headline.Text = "READY";
                PrimaryButton.Content = "PLAY";
                ProgressPanel.IsVisible = false;
                UpdateSummary();
                ReloadProfiles();
                if (_versionOffer is { } pending && StatusText.Text is null or "")
                {
                    StatusText.Foreground = Brush("AccentBrush");
                    StatusText.Text = pending.Note;
                }
                break;
            case Mode.Playing:
                Headline.Text = "RUNNING";
                Subhead.Text = "The game runs in its own window.";
                PrimaryButton.Content = "RUNNING";
                break;
            case Mode.Crashed:
                Headline.Text = "THE GAME CLOSED UNEXPECTEDLY";
                PrimaryButton.Content = "REPORT CRASH";
                ProgressPanel.IsVisible = false;
                break;
        }
        if (mode is Mode.Setup or Mode.Building) UpdateSetupRows();
        UpdateActions();
    }

    // Which actions are open: nothing runs twice, and Play waits for a panel's work.
    private void UpdateActions()
    {
        var running = _host?.GameRunning == true;
        GameBadge.IsVisible = Pfp4Install;
        PrimaryButton.IsEnabled = !_busy && _mode switch
        {
            Mode.Setup => SetupCanStart(),
            Mode.Ready => !running && ProfileCombo.SelectedIndex >= 0,
            Mode.Crashed => true,
            _ => false,
        };
        AltButton.IsEnabled = !_busy;
        NavPanel.IsEnabled = !_busy;
        ProfileCombo.IsEnabled = ProfileButton.IsEnabled = !_busy && !running;
        UpdateSettingsActions();
        UpdateControlsActions();
        UpdateUpdateActions();
        Disc2AddButtons.IsEnabled = DlcAddButtons.IsEnabled = ChooseModAgainButton.IsEnabled = UnlockUnicornsButton.IsEnabled = ContentOpen;
        UpdateSetupActions();
    }

    private void SetBusy(bool busy)
    {
        _busy = busy;
        UpdateActions();
    }

    private static string? DroppedPath(DragEventArgs e)
    {
        var paths = e.DataTransfer.TryGetFiles()?.Select(item => item.TryGetLocalPath()).OfType<string>().ToList();
        return paths is { Count: 1 } && (File.Exists(paths[0]) || Directory.Exists(paths[0])) ? paths[0] : null;
    }

    private bool AcceptsDrop => !_busy && (_mode == Mode.Setup || (_mode == Mode.Ready && _panel == Panel.Content && ContentOpen && !Pfp4Install));

    private bool Pfp4Install => _host?.Setup.Game == Core.Profiles.ProfileStore.Pfp4;

    private void OnDragOver(object? sender, DragEventArgs e)
    {
        var accepted = AcceptsDrop && DroppedPath(e) is not null;
        e.DragEffects = accepted ? DragDropEffects.Copy : DragDropEffects.None;
        DropOverlayText.Text = _mode != Mode.Setup ? "DROP TO ADD THIS DLC"
            : DropsGoToDisc1 ? "DROP TO USE THIS DISC 1" : Pfp4Chosen ? "DROP TO USE THIS MOD" : "DROP TO USE THIS DISC 2";
        DropOverlay.IsVisible = accepted;
    }

    private async void OnDrop(object? sender, DragEventArgs e)
    {
        DropOverlay.IsVisible = false;
        if (!AcceptsDrop || DroppedPath(e) is not { } path) return;
        if (_mode == Mode.Ready) await RunWorkAsync(t => AddDlcAsync([path], t));
        else if (DropsGoToDisc1) await ChooseDisc1Async(path);
        else if (Pfp4Chosen) await ChooseModAsync(path);
        else await ChooseDisc2Async(path);
    }

    private async void Primary_Click(object? sender, RoutedEventArgs e)
    {
        if (_host is null || _busy) return;
        if (_mode == Mode.Setup) await RunWorkAsync(RunSetupAsync);
        else if (_mode == Mode.Ready) await PlayAsync();
        else if (_mode == Mode.Crashed) ReportCrash();
    }

    private async void Alt_Click(object? sender, RoutedEventArgs e)
    {
        if (_host is null || _busy || _altAction is not { } action) return;
        await action();
    }

    private Func<Task>? _altAction;

    private void ShowAltButton(string label, Func<Task> action)
    {
        _altAction = action;
        AltButton.Content = label;
        AltButton.IsVisible = true;
    }

    // Runs one piece of long work with a fresh cancellation; closing the window cancels it and
    // waits until it has stopped, so setup's process tree is ended before the launcher exits.
    private async Task RunWorkAsync(Func<CancellationToken, Task> work)
    {
        using var cancel = new CancellationTokenSource();
        _cancel = cancel;
        SetBusy(true);
        try
        {
            _work = work(cancel.Token);
            await _work;
        }
        finally
        {
            _cancel = null;
            _work = null;
            SetBusy(false);
        }
    }

    private void AppendLog(string line)
    {
        lock (_sessionLogGate)
        {
            try { _sessionLog?.WriteLine($"[{DateTime.Now:HH:mm:ss}] {line}"); }
            catch (IOException) { _sessionLog = null; }
        }
        _pendingLog.Enqueue(line);
    }

    // Four times a second at most, whatever the rate of lines.
    private void FlushLog()
    {
        if (_pendingLog.IsEmpty) return;
        while (_pendingLog.TryDequeue(out var line))
        {
            _logTail.Enqueue(line);
            if (_logTail.Count > LogBoxLines) _logTail.Dequeue();
        }
        if (LogBox.IsVisible) ShowLogTail();
    }

    private void ShowLogTail()
    {
        LogBox.Text = string.Join(Environment.NewLine, _logTail);
        LogBox.CaretIndex = LogBox.Text.Length;
    }

    private void StartSessionLog()
    {
        if (_host is null) return;
        lock (_sessionLogGate)
        {
            try
            {
                _sessionLog?.Dispose();
                Directory.CreateDirectory(_host.Layout.Logs);
                _sessionLog = new StreamWriter(_host.Layout.LauncherLog, append: false) { AutoFlush = true };
            }
            catch (Exception error) when (error is IOException or UnauthorizedAccessException) { _sessionLog = null; }
        }
    }

    private void SetProgress(double percent, string? message)
    {
        ProgressPanel.IsVisible = true;
        CancelButton.IsVisible = _cancel is not null;
        Progress.IsVisible = true;
        Progress.IsIndeterminate = percent < 0;
        if (percent >= 0)
        {
            Progress.Value = percent;
            PercentText.Text = $"{percent:0}%";
        }
        else PercentText.Text = "";
        if (!string.IsNullOrWhiteSpace(message)) ProgressMessage.Text = message;
    }

    // A failure the player can act on, with the primary button to try again.
    private void ShowFailure(string headline, string message, Mode mode = Mode.Ready)
    {
        ShowMode(mode);
        Headline.Text = headline;
        Subhead.Text = message;
        // The panel keeps only the details; the headline says what happened.
        ProgressPanel.IsVisible = true;
        CancelButton.IsVisible = false;
        Progress.IsVisible = false;
        ProgressMessage.Text = "";
        PercentText.Text = "";
        if (mode == Mode.Ready) PrimaryButton.Content = "TRY AGAIN";
        AppendLog($"ERROR: {message}");
    }

    private void Details_Click(object? sender, RoutedEventArgs e)
    {
        LogBox.IsVisible = !LogBox.IsVisible;
        DetailsButton.Content = LogBox.IsVisible ? "HIDE DETAILS" : "SHOW DETAILS";
        if (LogBox.IsVisible) ShowLogTail();
    }

    private void Cancel_Click(object? sender, RoutedEventArgs e) => _cancel?.Cancel();

    private void Saves_Click(object? sender, RoutedEventArgs e)
    {
        if (_host is null) return;
        var folder = SelectedProfile?.User ?? _host.Layout.Profiles;
        Directory.CreateDirectory(folder);
        LauncherHost.Open(folder);
    }

    private void Logs_Click(object? sender, RoutedEventArgs e)
    {
        if (_host is null)
        {
            LauncherHost.Open(AppContext.BaseDirectory);
            return;
        }
        if (_mode == Mode.Crashed && _pendingReport is { } report)
        {
            LauncherHost.Reveal(report.Bundle);
            return;
        }
        // The game's own logs once it has run, else the active version's setup logs.
        var setupLogs = _host.Layout.SetupLogs(_host.Settings.ActiveVersion ?? _host.Payload.Version);
        var folder = File.Exists(_host.Layout.RuntimeLog) || !Directory.Exists(setupLogs) ? _host.Layout.Logs : setupLogs;
        Directory.CreateDirectory(folder);
        LauncherHost.Open(folder);
    }

    private void Report_Click(object? sender, RoutedEventArgs e) => LauncherHost.Open($"{Repository}/issues/new");

    private void GitHub_Click(object? sender, RoutedEventArgs e) => LauncherHost.Open(Repository);

    protected override async void OnClosing(WindowClosingEventArgs e)
    {
        if (_work is { IsCompleted: false } work)
        {
            e.Cancel = true;
            if (_closing) return;
            _closing = true;
            _cancel?.Cancel();
            try { await work; }
            catch (Exception) { }
            Close();
            return;
        }
        StopWheelProbe();
        lock (_sessionLogGate)
        {
            _sessionLog?.Dispose();
            _sessionLog = null;
        }
        base.OnClosing(e);
    }
}
