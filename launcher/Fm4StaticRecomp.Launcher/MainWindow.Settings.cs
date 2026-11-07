// Portions copyright (c) 2026, Pinyon Shift contributors (BSD 3-Clause: see THIRD_PARTY_NOTICES.md).
// Copyright (c) 2026, i2kiselev (BSD 3-Clause: see LICENSE in the repository root).
// Adapted from Pinyon Shift's launcher (launcher/PinyonShift.Launcher.Desktop, BSD-3-Clause; see THIRD_PARTY_NOTICES.md).
using System.Globalization;
using Avalonia;
using Avalonia.Automation;
using Avalonia.Controls;
using Avalonia.Controls.Primitives;
using Avalonia.Interactivity;
using Avalonia.Layout;
using Avalonia.Media;
using Avalonia.Threading;
using Fm4StaticRecomp.Launcher.Controls;
using Fm4StaticRecomp.Launcher.Core.Pfp4;
using Fm4StaticRecomp.Launcher.Core.Profiles;
using Fm4StaticRecomp.Launcher.Core.Settings;

namespace Fm4StaticRecomp.Launcher;

// The GRAPHICS and SETTINGS panels (spec 8): the game's GRAPHICS page's rows from the schema the
// build wrote, and the launch and advanced options. Nothing is written until SAVE, which writes both.
public partial class MainWindow
{
    private const string Custom = "CUSTOM";

    private GraphicsSettings? _graphics;
    private string? _graphicsProblem;
    private bool _skipIntro, _asyncReadbacks, _presentViaDxgi, _compat, _showFps, _controllerVibration, _diagnostic30Fps, _windowed;
    private bool _audioPopFix = true;
    private (int Width, int Height) _windowSize = WindowSizes.Default;
    private int _buildJobs, _vibrationStrength = LauncherSettings.GameVibrationStrength;
    private string _language = GameLanguages.Default.Code;
    private string _commandLineOptions = "";
    private string? _savesFolder, _savesNote;
    private string _pfp4Mode = DefaultMode;
    private bool _pfp4HarderAi;
    private Button? _saveSettingsButton, _restoreSettingsButton;
    private TextBlock? _settingsStatus, _unsavedMark;
    private int? _refreshHz;
    private (int Width, int Height)? _displaySize, _primarySize;
    private string? _upscalerNote;

    // What the game's picture ends up on, which decides whether an upscaler runs.
    private (int Width, int Height)? OutputSize => _windowed ? _windowSize : _displaySize;

    private static readonly string[] AntiAliasingKeys = ["fm4_showroom_msaa", "fm4_race_msaa", "swap_post_effect"];

    private void LoadSettingsPanel()
    {
        if (_host is not { } host) return;
        _refreshHz = DisplayInfo.RefreshHz(this);
        _displaySize = DisplayInfo.Size(this);
        _primarySize = DisplayInfo.PrimarySize();
        try
        {
            _graphics = host.Graphics();
            _graphicsProblem = null;
        }
        catch (Exception error) when (error is IOException or InvalidDataException or System.Text.Json.JsonException)
        {
            _graphics = null;
            _graphicsProblem = $"The graphics settings list could not be read: {error.Message}";
        }
        var settings = host.Settings;
        (_skipIntro, _asyncReadbacks, _presentViaDxgi, _compat, _showFps, _controllerVibration, _buildJobs, _language) =
            (settings.SkipIntro, settings.AsyncReadbacks, settings.PresentViaDxgi, settings.Compat, settings.ShowFps, settings.ControllerVibration, settings.BuildJobs, settings.Language);
        (_windowed, _windowSize) = (settings.Windowed, settings.WindowSize);
        _diagnostic30Fps = settings.Diagnostic30Fps;
        _audioPopFix = settings.AudioPopFix;
        _vibrationStrength = settings.VibrationStrength;
        _commandLineOptions = settings.CommandLineOptions;
        (_savesFolder, _savesNote) = (settings.SavesFolder, null);
        (_pfp4Mode, _pfp4HarderAi) = (host.Setup.Pfp4Mode ?? DefaultMode, host.Setup.Pfp4HarderAi);
        BuildSettingsPanel("");
    }

    private void BuildSettingsPanel(string status)
    {
        if (_host is not { } host) return;
        SettingsBody.Children.Clear();
        _presetCombo = null;
        var graphicsPage = _panel == Panel.Graphics;
        SettingsBody.Children.Add(new VectorHeading
            { Text = graphicsPage ? "GRAPHICS" : "SETTINGS", FontSize = 24, Foreground = Brush("LabelBrush") });

        // On top: below a long page a player stopped short of SAVE and took it for missing.
        _unsavedMark = new TextBlock { Text = "*", Foreground = Brush("AccentBrush"), FontWeight = FontWeight.Bold };
        _saveSettingsButton = new Button
        {
            Content = new StackPanel { Orientation = Orientation.Horizontal, Spacing = 6, Children = { new TextBlock { Text = "SAVE" }, _unsavedMark } },
            MinWidth = 120, HorizontalContentAlignment = HorizontalAlignment.Center, Margin = new Thickness(0, 0, 10, 0), Classes = { "quiet" },
        };
        AutomationProperties.SetName(_saveSettingsButton, "SAVE");
        _saveSettingsButton.Click += (_, _) => SaveSettings();
        var actions = new WrapPanel { Children = { _saveSettingsButton } };
        _restoreSettingsButton = null;
        if (graphicsPage)
        {
            var reset = new Button { Content = "RESET TO DEFAULTS", Margin = new Thickness(0, 0, 10, 0), Classes = { "quiet" } };
            reset.Click += (_, _) =>
            {
                _graphics?.ResetToDefaults();
                (_windowed, _windowSize) = (false, WindowSizes.Default);
                BuildSettingsPanel("Defaults shown; SAVE writes them.");
            };
            _restoreSettingsButton = new Button { Content = "RESTORE BACKUP", Classes = { "link" }, VerticalAlignment = VerticalAlignment.Center };
            _restoreSettingsButton.Click += (_, _) => RestoreSettingsBackup();
            actions.Children.Add(reset);
            actions.Children.Add(_restoreSettingsButton);
        }
        SettingsBody.Children.Add(actions);
        _settingsStatus = Help(status);
        SettingsBody.Children.Add(_settingsStatus);

        if (graphicsPage) AddGraphicsRows();
        else AddOtherRows(host);
        RefreshUnsavedMark();
        UpdateActions();
    }

    // Every row's change bubbles up here; the mark is set after the row's own handler has stored it.
    private void WatchSettingsEdits()
    {
        foreach (var edit in new RoutedEvent[]
                 {
                     SelectingItemsControl.SelectionChangedEvent, RangeBase.ValueChangedEvent, TextBox.TextChangedEvent,
                     ToggleButton.IsCheckedChangedEvent, NumericUpDown.ValueChangedEvent,
                 })
            SettingsBody.AddHandler(edit, new EventHandler<RoutedEventArgs>((_, _) => Dispatcher.UIThread.Post(RefreshUnsavedMark)),
                handledEventsToo: true);
    }

    private void RefreshUnsavedMark()
    {
        if (_unsavedMark is null || _saveSettingsButton is null) return;
        _unsavedMark.IsVisible = SettingsUnsaved();
        ToolTip.SetTip(_saveSettingsButton, _unsavedMark.IsVisible ? "Changed, not saved yet." : null);
    }

    private bool SettingsUnsaved()
    {
        if (_host is not { } host) return false;
        var settings = host.Settings;
        return _graphics?.Changed == true
               || (_skipIntro, _asyncReadbacks, _presentViaDxgi, _compat, _showFps, _controllerVibration, _buildJobs, _language, _commandLineOptions.Trim())
               != (settings.SkipIntro, settings.AsyncReadbacks, settings.PresentViaDxgi, settings.Compat, settings.ShowFps, settings.ControllerVibration, settings.BuildJobs,
                   settings.Language, settings.CommandLineOptions)
               || (_windowed, _windowSize) != (settings.Windowed, settings.WindowSize)
               || _diagnostic30Fps != settings.Diagnostic30Fps
               || _audioPopFix != settings.AudioPopFix
               || _vibrationStrength != settings.VibrationStrength
               || !string.Equals(_savesFolder, settings.SavesFolder, StringComparison.OrdinalIgnoreCase)
               || (Pfp4Install && (_pfp4Mode != (host.Setup.Pfp4Mode ?? DefaultMode) || _pfp4HarderAi != host.Setup.Pfp4HarderAi));
    }

    private void AddGraphicsRows()
    {
        AddWindowRows();
        if (_graphics is not { } graphics)
        {
            SettingsBody.Children.Add(Help(_graphicsProblem ?? "Build the game first."));
            return;
        }
        if (graphics.FileIsMalformed)
            SettingsBody.Children.Add(Help("The settings file could not be read; the defaults are shown. Saving backs the file up first."));
        foreach (var block in graphics.Schema.Blocks)
        {
            var rows = new StackPanel { Spacing = 6, Margin = new Thickness(0, 4, 0, 10) };
            foreach (var row in block.Rows.Where(graphics.Shows)) rows.Children.Add(GraphicsRow(graphics, row));
            SettingsBody.Children.Add(rows);
        }
    }

    // The window is the output in windowed play, so the scale and upscaler rows below follow it.
    private void AddWindowRows()
    {
        var rows = new StackPanel { Spacing = 6, Margin = new Thickness(0, 4, 0, 10) };
        rows.Children.Add(Option("WINDOWED", "The game starts in a window you can resize. Alt+Enter switches between window and fullscreen while you play.",
            _windowed, v =>
            {
                _windowed = v;
                OutputSizeChanged();
            }));
        var sizes = WindowSizes.Fitting(_primarySize, _windowSize);
        var size = Combo(sizes.Select(WindowSizes.Label).ToList(), sizes.ToList().IndexOf(_windowSize));
        size.SelectionChanged += (_, _) =>
        {
            if (size.SelectedIndex < 0) return;
            _windowSize = sizes[size.SelectedIndex];
            if (_windowed) OutputSizeChanged();
        };
        rows.Children.Add(Labelled("WINDOW SIZE", size, "The size the window starts at, and the size Alt+Enter leaves fullscreen to.", alwaysHelp: true));
        SettingsBody.Children.Add(rows);
    }

    private void OutputSizeChanged()
    {
        if (_graphics is { } graphics) NoteLoweredScale(graphics);
        BuildSettingsPanel("");
    }

    private void NoteLoweredScale(GraphicsSettings graphics)
    {
        if (graphics.FitScaleToUpscaler(OutputSize) is { } lowered)
            _upscalerNote = $"Resolution scale lowered to {lowered}x: {graphics.RunningUpscaler(OutputSize)} runs up to "
                            + $"{lowered}x on this {OutputSize!.Value.Width}x{OutputSize.Value.Height} {(_windowed ? "window" : "screen")}.";
    }

    private void AddOtherRows(LauncherHost host)
    {
        if (Pfp4Install)
        {
            SettingsBody.Children.Add(Header("PROJECT FORZA PLUS 4"));
            var mode = Combo(Pfp4ModeNames.Select(m => m.ToUpperInvariant()).ToList(), Array.IndexOf(Pfp4ModeNames, _pfp4Mode));
            mode.SelectionChanged += (_, _) =>
            {
                if (mode.SelectedIndex >= 0) _pfp4Mode = Pfp4ModeNames[mode.SelectedIndex];
            };
            SettingsBody.Children.Add(Labelled("CAREER MODE", mode,
                ModeHelp + " A career started in another mode loads with a warning, once.", alwaysHelp: true));
            SettingsBody.Children.Add(Option("HARDER AI", HarderAiHelp, _pfp4HarderAi, v => _pfp4HarderAi = v));
        }

        SettingsBody.Children.Add(Header("LAUNCH OPTIONS"));
        var languages = GameLanguages.All;
        var language = Combo(languages.Select(l => l.Name).ToList(), languages.ToList().FindIndex(l => l.Code == _language));
        language.SelectionChanged += (_, _) =>
        {
            if (language.SelectedIndex >= 0) _language = languages[language.SelectedIndex].Code;
        };
        SettingsBody.Children.Add(Labelled("LANGUAGE", language,
            "The game's text and voice-over. Korean, Chinese, Portuguese, Czech, Hungarian and Dutch keep the English voice-over.",
            alwaysHelp: true));
        SettingsBody.Children.Add(Option("SKIP INTRO VIDEOS", "The game starts at its title screen.", _skipIntro, v => _skipIntro = v));
        SettingsBody.Children.Add(Option("SHOW FPS", "Displays the real-time frame rate and frame timing overlay.", _showFps, v => _showFps = v));
        SettingsBody.Children.Add(Option("CONTROLLER VIBRATION", "Controller rumble motors for impacts, road bumps and engine revs.", _controllerVibration, v => _controllerVibration = v));
        SettingsBody.Children.Add(Option("AUDIO POP FIX",
            "Smooths the pops in the engine sounds, which a console has too: where a loop starts over, and where the game "
            + "restarts sounds while revving and shifting. Off plays the audio as the console does. Versions before 0.3.2 "
            + "always play it as the console does.", _audioPopFix, v => _audioPopFix = v));
        var vibration = new Slider
        {
            Minimum = 0, Maximum = LauncherSettings.MaximumVibrationStrength, Value = _vibrationStrength, SmallChange = 1,
            LargeChange = 10, IsSnapToTickEnabled = true, TickFrequency = 1, Width = 320,
        };
        var vibrationValue = new TextBlock { Text = $"{_vibrationStrength}%", Width = 48, VerticalAlignment = VerticalAlignment.Center,
            Foreground = Brush("ValueBrush") };
        vibration.ValueChanged += (_, e) =>
        {
            _vibrationStrength = (int)Math.Round(e.NewValue);
            vibrationValue.Text = $"{_vibrationStrength}%";
        };
        AutomationProperties.SetName(vibration, "VIBRATION STRENGTH");
        SettingsBody.Children.Add(Labelled("VIBRATION STRENGTH",
            new StackPanel { Orientation = Orientation.Horizontal, Spacing = 10, Children = { vibration, vibrationValue } },
            "100% is the game's own strength. Above 100% the controller's motors run stronger, 0% turns vibration off. "
            + "Versions before 0.3.2 always use 100%.", alwaysHelp: true, focus: vibration));

        SettingsBody.Children.Add(Header("ADVANCED"));
        SettingsBody.Children.Add(Help("These are the recommended defaults. Don't change them unless you're sure what a setting does."));
        SettingsBody.Children.Add(Option("ASYNC READBACKS",
            "Smoother races. In long sessions a texture is rarely drawn wrong until the next race.", _asyncReadbacks, v => _asyncReadbacks = v));
        if (host.Platform.SupportsDxgiPresentation)
            SettingsBody.Children.Add(Option("PRESENT THROUGH DXGI", "For frame generation and capture tools.", _presentViaDxgi,
                v => _presentViaDxgi = v));
        SettingsBody.Children.Add(Option("COMPAT MODE",
            "Every rendering shortcut off: tells a rendering bug of ours from the game's own look.", _compat, v => _compat = v));
        SettingsBody.Children.Add(Option("30 FPS (DIAGNOSTIC)",
            "For testing slow processors: the game runs at 30 frames per second, whatever GRAPHICS says. Not for normal play.",
            _diagnostic30Fps, v => _diagnostic30Fps = v));
        var commandLine = new TextBox { Text = _commandLineOptions, Watermark = "--name=value --other=value", Width = 420,
            HorizontalAlignment = HorizontalAlignment.Left };
        commandLine.TextChanged += (_, _) => _commandLineOptions = commandLine.Text ?? "";
        SettingsBody.Children.Add(Labelled("COMMAND-LINE OPTIONS", commandLine,
            "Game options for debugging, passed on its command line. One replaces the value the launcher or the GRAPHICS page would use. A misspelt name is ignored without a message.",
            alwaysHelp: true));
        SettingsBody.Children.Add(SavesFolderRow(host));
        var jobs = new NumericUpDown
        {
            Minimum = 0, Maximum = LauncherSettings.MaximumBuildJobs, Increment = 1, Value = _buildJobs, FormatString = "0",
            Width = 140, HorizontalAlignment = HorizontalAlignment.Left,
        };
        AutomationProperties.SetName(jobs, "BUILD JOBS");
        jobs.ValueChanged += (_, e) => _buildJobs = (int)(e.NewValue ?? 0);
        SettingsBody.Children.Add(Labelled("BUILD JOBS", jobs, "0 picks them from your processor and memory.", alwaysHelp: true));
    }

    // The folder is checked when chosen; SAVE moves the saves.
    private Control SavesFolderRow(LauncherHost host)
    {
        var path = new TextBlock { TextWrapping = TextWrapping.Wrap, Foreground = Brush("ValueBrush"), VerticalAlignment = VerticalAlignment.Center };
        void Show() => path.Text = _savesFolder ?? $"{host.Layout.Root} (default)";
        Show();
        var choose = new Button { Content = "CHOOSE…", Classes = { "quiet" } };
        choose.Click += async (_, _) =>
        {
            if (_busy || await PickFolderAsync("Choose a folder for your saves") is not { } folder) return;
            folder = Path.TrimEndingDirectorySeparator(Path.GetFullPath(folder));
            var game = host.Setup.Game ?? "vanilla";
            string? problem;
            IReadOnlyList<string> others;
            try
            {
                problem = SavesFolder.Problem(host.Layout, folder);
                others = problem is null ? SavesFolder.OtherGameProfiles(folder, game) : [];
            }
            catch (Exception error) when (error is IOException or UnauthorizedAccessException)
            {
                problem = $"{folder} cannot be used: {error.Message}";
                others = [];
            }
            if (problem is not null)
            {
                if (_settingsStatus is not null) _settingsStatus.Text = problem;
                return;
            }
            string? note = null;
            if (others.Count > 0)
            {
                if (await Dialog.AskAsync(this, "SAVES OF THE OTHER GAME", SavesFolder.OtherGameWarning(game, others),
                        "CANCEL", "USE ANYWAY") != "USE ANYWAY") return;
                note = $"{GameName(game == ProfileStore.Pfp4 ? "vanilla" : ProfileStore.Pfp4)} saves taken over by SAVES FOLDER into this install";
            }
            (_savesFolder, _savesNote) = (folder.Equals(host.Layout.Root, StringComparison.OrdinalIgnoreCase) ? null : folder, note);
            Show();
            if (_settingsStatus is not null) _settingsStatus.Text = "";
            RefreshUnsavedMark();
        };
        var reset = new Button { Content = "DEFAULT", Classes = { "link" }, VerticalAlignment = VerticalAlignment.Center };
        reset.Click += (_, _) =>
        {
            if (_busy) return;
            (_savesFolder, _savesNote) = (null, null);
            Show();
            RefreshUnsavedMark();
        };
        var row = new StackPanel { Spacing = 4, Children = { path, new StackPanel { Orientation = Orientation.Horizontal, Spacing = 10, Children = { choose, reset } } } };
        return Labelled("SAVES FOLDER", row,
            "Where your profiles, their backups and deleted profiles are kept. SAVE moves them to the folder chosen; " +
            "if it holds saves already (an earlier install's), both sets are kept there. " +
            "Saves of vanilla FM4 on a Project Forza Plus 4 install (or the other way round) are not guaranteed to work: start a fresh career there.",
            alwaysHelp: true);
    }

    private void UpdateSettingsActions()
    {
        var open = _host is { GameRunning: false } && !_busy;
        if (_saveSettingsButton is not null) _saveSettingsButton.IsEnabled = open;
        if (_restoreSettingsButton is not null) _restoreSettingsButton.IsEnabled = open;
    }

    private Control GraphicsRow(GraphicsSettings graphics, SchemaRow row)
    {
        switch (row.Kind)
        {
            case "preset":
            {
                var labels = graphics.Schema.Presets.Select(p => p.Label).Append(Custom).ToList();
                var combo = Combo(labels, labels.IndexOf(graphics.CurrentPreset() ?? Custom));
                combo.SelectionChanged += (_, _) =>
                {
                    if (_syncingPreset || combo.SelectedItem is not string label || label == Custom) return;
                    graphics.ApplyPreset(label);
                    BuildSettingsPanel("");
                };
                return Labelled(row.Label, combo, row.Description);
            }
            case "range":
            {
                var high = row.Key == "fm4_frame_rate" ? graphics.FrameRateCeiling(_refreshHz) : row.High;
                var current = int.TryParse(graphics.Value(row.Key!), out var n) ? Math.Clamp(n, row.Low, high) : row.Low;
                var slider = new Slider
                {
                    Minimum = row.Low, Maximum = high, Value = current, SmallChange = 1, LargeChange = 10,
                    IsSnapToTickEnabled = true, TickFrequency = 1, Width = 320,
                };
                var value = new TextBlock { Text = current.ToString(CultureInfo.InvariantCulture), Width = 48,
                    VerticalAlignment = VerticalAlignment.Center, Foreground = Brush("ValueBrush") };
                var top = new TextBlock { Text = high.ToString(CultureInfo.InvariantCulture), Classes = { "help" },
                    VerticalAlignment = VerticalAlignment.Center };
                slider.ValueChanged += (_, e) =>
                {
                    var chosen = (int)Math.Round(e.NewValue);
                    value.Text = chosen.ToString(CultureInfo.InvariantCulture);
                    graphics.Set(row.Key!, value.Text);
                    RefreshPresetCombo(graphics);
                };
                AutomationProperties.SetName(slider, row.Label);
                var line = new StackPanel { Orientation = Orientation.Horizontal, Spacing = 10, Children = { slider, value, top } };
                return Labelled(row.Label, line, row.Description, focus: slider);
            }
            case "choice":
            {
                // As on the game's GRAPHICS page, a running upscaler takes over the anti-aliasing:
                // the row says so and keeps the player's choice for later.
                if (AntiAliasingKeys.Contains(row.Key) && graphics.TemporalUpscaler(OutputSize) is { } upscaler)
                    return Labelled(row.Label, Combo([$"OFF ({upscaler})"], 0, enabled: false), row.Description);
                var upscalerRow = row.Key == "present_effect";
                var choices = row.Key == "resolution_scale" ? graphics.ScaleChoices(OutputSize) : row.Choices!;
                var index = choices.Select(c => c.Value).ToList().IndexOf(graphics.Value(row.Key!));
                var combo = Combo(choices.Select(c => c.Label).ToList(), index);
                combo.SelectionChanged += (_, _) =>
                {
                    if (combo.SelectedIndex < 0) return;
                    graphics.Set(row.Key!, choices[combo.SelectedIndex].Value);
                    if (upscalerRow) NoteLoweredScale(graphics);
                    // Rows shown only for some of this setting's values, and the rows the upscaler
                    // and scale decide, follow it.
                    if (upscalerRow || row.Key == "resolution_scale"
                        || graphics.Schema.SettingRows.Any(r => r.ShownWhen?.ContainsKey(row.Key!) == true))
                        BuildSettingsPanel("");
                    else
                        RefreshPresetCombo(graphics);
                };
                var labelled = Labelled(row.Label, combo, row.Description);
                if (upscalerRow && _upscalerNote is { } note)
                {
                    _upscalerNote = null;
                    return new StackPanel { Spacing = 2, Children = { labelled, Help(note) } };
                }
                return labelled;
            }
            default:
                return new TextBlock { Text = row.Label };
        }
    }

    private ComboBox? _presetCombo;
    private bool _syncingPreset;

    // A row changed by hand shows the preset it now matches, or CUSTOM, without applying it.
    private void RefreshPresetCombo(GraphicsSettings graphics)
    {
        if (_presetCombo is not { } combo) return;
        _syncingPreset = true;
        combo.SelectedItem = graphics.CurrentPreset() ?? Custom;
        _syncingPreset = false;
    }

    private ComboBox Combo(IReadOnlyList<string> labels, int selected, bool enabled = true)
    {
        var combo = new ComboBox { ItemsSource = labels, SelectedIndex = selected, Width = 220, IsEnabled = enabled };
        if (labels.Contains(Custom)) _presetCombo = combo;
        return combo;
    }

    // A caps label left, the control right, and the description under it while the row has focus.
    private Control Labelled(string label, Control control, string description, Control? focus = null, bool alwaysHelp = false)
    {
        AutomationProperties.SetName(control, label);
        var help = Help(description);
        help.IsVisible = alwaysHelp;
        var target = focus ?? control;
        if (!alwaysHelp)
        {
            target.GotFocus += (_, _) => help.IsVisible = true;
            target.LostFocus += (_, _) => help.IsVisible = false;
        }
        var grid = new Grid { ColumnDefinitions = new ColumnDefinitions("260,*") };
        grid.Children.Add(new TextBlock { Text = label, VerticalAlignment = VerticalAlignment.Center });
        Grid.SetColumn(control, 1);
        grid.Children.Add(control);
        return new StackPanel { Spacing = 2, Children = { grid, help } };
    }

    private Control Option(string label, string description, bool value, Action<bool> changed)
    {
        var box = new CheckBox { IsChecked = value, Content = new TextBlock { Text = label } };
        AutomationProperties.SetName(box, label);
        box.IsCheckedChanged += (_, _) => changed(box.IsChecked == true);
        return new StackPanel { Spacing = 0, Children = { box, Help(description, new Thickness(30, 0, 0, 4)) } };
    }

    private static TextBlock Header(string text) =>
        new() { Text = text, Classes = { "header" }, Margin = new Thickness(0, 12, 0, 2) };

    private static TextBlock Help(string text, Thickness? margin = null) =>
        new() { Text = text, Classes = { "help" }, TextWrapping = TextWrapping.Wrap, Margin = margin ?? default };

    private async void SaveSettings()
    {
        if (_host is not { GameRunning: false } host || _busy) return;
        try
        {
            string? leftover = null;
            if (!string.Equals(_savesFolder, host.Settings.SavesFolder, StringComparison.OrdinalIgnoreCase))
            {
                SetBusy(true);
                // As _work, closing the window waits for the move instead of cutting a copy short.
                var move = host.MoveSavesAsync(_savesFolder, _savesNote);
                _work = move;
                try
                {
                    leftover = await move;
                }
                finally
                {
                    _work = null;
                    SetBusy(false);
                }
                AppendLog($"Saves moved to {host.Layout.SavesRoot}.");
                if (leftover is not null) AppendLog(leftover);
                ReloadProfiles();
            }
            if (Pfp4Install && (_pfp4Mode != host.Setup.Pfp4Mode || _pfp4HarderAi != host.Setup.Pfp4HarderAi))
            {
                Pfp4Modes.Switch(host.Layout, host.ModManifest(host.Settings.ActiveVersion ?? host.Payload.Version), host.Setup,
                    _pfp4Mode, _pfp4HarderAi);
                AppendLog($"Project Forza Plus 4: {ModeText(_pfp4Mode, _pfp4HarderAi)}.");
            }
            _graphics?.Save();
            var settings = host.Settings;
            (settings.SkipIntro, settings.AsyncReadbacks, settings.PresentViaDxgi, settings.Compat, settings.ShowFps, settings.ControllerVibration, settings.BuildJobs, settings.Language) =
                (_skipIntro, _asyncReadbacks, _presentViaDxgi, _compat, _showFps, _controllerVibration, _buildJobs, _language);
            (settings.Windowed, settings.WindowSize) = (_windowed, _windowSize);
            settings.Diagnostic30Fps = _diagnostic30Fps;
            settings.AudioPopFix = _audioPopFix;
            settings.VibrationStrength = _vibrationStrength;
            settings.CommandLineOptions = _commandLineOptions.Trim();
            settings.Save();
            if (_settingsStatus is not null) _settingsStatus.Text = leftover ?? "Saved. Applies at the next start.";
            RefreshUnsavedMark();
            UpdateSummary();
            AppendLog("Settings saved.");
        }
        catch (Exception error) when (error is IOException or UnauthorizedAccessException or InvalidDataException)
        {
            if (_settingsStatus is not null) _settingsStatus.Text = $"Not saved: {error.Message}";
        }
    }

    private void RestoreSettingsBackup()
    {
        if (_host is not { GameRunning: false } || _graphics is not { } graphics) return;
        try
        {
            BuildSettingsPanel(graphics.RestoreBackup() ? "Backup restored. Applies at the next start." : "There is no backup yet.");
        }
        catch (Exception error) when (error is IOException or UnauthorizedAccessException)
        {
            if (_settingsStatus is not null) _settingsStatus.Text = $"Not restored: {error.Message}";
        }
    }
}
