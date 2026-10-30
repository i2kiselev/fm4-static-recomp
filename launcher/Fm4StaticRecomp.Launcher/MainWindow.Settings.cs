// Adapted from Pinyon Shift's launcher (launcher/PinyonShift.Launcher.Desktop, BSD-3-Clause; see THIRD_PARTY_NOTICES.md).
using System.Globalization;
using Avalonia;
using Avalonia.Automation;
using Avalonia.Controls;
using Avalonia.Layout;
using Avalonia.Media;
using Fm4StaticRecomp.Launcher.Controls;
using Fm4StaticRecomp.Launcher.Core.Settings;

namespace Fm4StaticRecomp.Launcher;

// The Settings panel (spec 8): the GRAPHICS page's rows from the schema the build wrote,
// the launch options and the advanced options. Nothing is written until SAVE.
public partial class MainWindow
{
    private const string Custom = "CUSTOM";

    private GraphicsSettings? _graphics;
    private string? _graphicsProblem;
    private bool _skipIntro, _asyncReadbacks, _presentViaDxgi, _compat;
    private int _buildJobs;
    private string _language = GameLanguages.Default.Code;
    private string _commandLineOptions = "";
    private Button? _saveSettingsButton, _restoreSettingsButton;
    private TextBlock? _settingsStatus;
    private int? _refreshHz;
    private (int Width, int Height)? _displaySize;
    private string? _upscalerNote;

    private static readonly string[] AntiAliasingKeys = ["fm4_showroom_msaa", "fm4_race_msaa", "swap_post_effect"];

    private void LoadSettingsPanel()
    {
        if (_host is not { } host) return;
        _refreshHz = DisplayInfo.RefreshHz(this);
        _displaySize = DisplayInfo.Size(this);
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
        (_skipIntro, _asyncReadbacks, _presentViaDxgi, _compat, _buildJobs, _language) =
            (settings.SkipIntro, settings.AsyncReadbacks, settings.PresentViaDxgi, settings.Compat, settings.BuildJobs, settings.Language);
        _commandLineOptions = settings.CommandLineOptions;
        BuildSettingsPanel("");
    }

    private void BuildSettingsPanel(string status)
    {
        if (_host is not { } host) return;
        SettingsBody.Children.Clear();
        _presetCombo = null;
        SettingsBody.Children.Add(new VectorHeading { Text = "SETTINGS", FontSize = 24, Foreground = Brush("LabelBrush") });

        SettingsBody.Children.Add(Header("GRAPHICS"));
        if (_graphics is not { } graphics)
            SettingsBody.Children.Add(Help(_graphicsProblem ?? "Build the game first."));
        else
        {
            if (graphics.FileIsMalformed)
                SettingsBody.Children.Add(Help("The settings file could not be read; the defaults are shown. Saving backs the file up first."));
            foreach (var block in graphics.Schema.Blocks)
            {
                var rows = new StackPanel { Spacing = 6, Margin = new Thickness(0, 4, 0, 10) };
                foreach (var row in block.Rows.Where(graphics.Shows)) rows.Children.Add(GraphicsRow(graphics, row));
                SettingsBody.Children.Add(rows);
            }
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

        SettingsBody.Children.Add(Header("ADVANCED"));
        SettingsBody.Children.Add(Help("These are the recommended defaults. Don't change them unless you're sure what a setting does."));
        SettingsBody.Children.Add(Option("ASYNC READBACKS",
            "Smoother races. In long sessions a texture is rarely drawn wrong until the next race.", _asyncReadbacks, v => _asyncReadbacks = v));
        if (host.Platform.SupportsDxgiPresentation)
            SettingsBody.Children.Add(Option("PRESENT THROUGH DXGI", "For frame generation and capture tools.", _presentViaDxgi,
                v => _presentViaDxgi = v));
        SettingsBody.Children.Add(Option("COMPAT MODE",
            "Every rendering shortcut off: tells a rendering bug of ours from the game's own look.", _compat, v => _compat = v));
        var commandLine = new TextBox { Text = _commandLineOptions, Watermark = "--name=value --other=value", Width = 420,
            HorizontalAlignment = HorizontalAlignment.Left };
        commandLine.TextChanged += (_, _) => _commandLineOptions = commandLine.Text ?? "";
        SettingsBody.Children.Add(Labelled("COMMAND-LINE OPTIONS", commandLine,
            "Game options for debugging, passed on its command line. One replaces the value the launcher or the GRAPHICS page would use. A misspelt name is ignored without a message.",
            alwaysHelp: true));
        var jobs = new NumericUpDown
        {
            Minimum = 0, Maximum = LauncherSettings.MaximumBuildJobs, Increment = 1, Value = _buildJobs, FormatString = "0",
            Width = 140, HorizontalAlignment = HorizontalAlignment.Left,
        };
        AutomationProperties.SetName(jobs, "BUILD JOBS");
        jobs.ValueChanged += (_, e) => _buildJobs = (int)(e.NewValue ?? 0);
        SettingsBody.Children.Add(Labelled("BUILD JOBS", jobs, "0 picks them from your processor and memory.", alwaysHelp: true));

        _saveSettingsButton = new Button { Content = "SAVE", MinWidth = 120, Margin = new Thickness(0, 0, 10, 8), Classes = { "quiet" } };
        _saveSettingsButton.Click += (_, _) => SaveSettings();
        var reset = new Button { Content = "RESET TO DEFAULTS", Margin = new Thickness(0, 0, 10, 8), Classes = { "quiet" } };
        reset.Click += (_, _) =>
        {
            _graphics?.ResetToDefaults();
            BuildSettingsPanel("Defaults shown; SAVE writes them.");
        };
        _restoreSettingsButton = new Button { Content = "RESTORE BACKUP", Margin = new Thickness(0, 0, 0, 8), Classes = { "link" },
            VerticalAlignment = VerticalAlignment.Center };
        _restoreSettingsButton.Click += (_, _) => RestoreSettingsBackup();
        SettingsBody.Children.Add(new WrapPanel
            { Margin = new Thickness(0, 10, 0, 0), Children = { _saveSettingsButton, reset, _restoreSettingsButton } });
        _settingsStatus = Help(status);
        SettingsBody.Children.Add(_settingsStatus);
        UpdateActions();
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
                if (AntiAliasingKeys.Contains(row.Key) && graphics.TemporalUpscaler(_displaySize) is { } upscaler)
                    return Labelled(row.Label, Combo([$"OFF ({upscaler})"], 0, enabled: false), row.Description);
                var upscalerRow = row.Key == "present_effect";
                var choices = row.Key == "resolution_scale" ? graphics.ScaleChoices(_displaySize) : row.Choices!;
                var index = choices.Select(c => c.Value).ToList().IndexOf(graphics.Value(row.Key!));
                var combo = Combo(choices.Select(c => c.Label).ToList(), index);
                combo.SelectionChanged += (_, _) =>
                {
                    if (combo.SelectedIndex < 0) return;
                    graphics.Set(row.Key!, choices[combo.SelectedIndex].Value);
                    if (upscalerRow && graphics.FitScaleToUpscaler(_displaySize) is { } lowered)
                        _upscalerNote = $"Resolution scale lowered to {lowered}x: {graphics.RunningUpscaler(_displaySize)} runs up to "
                                        + $"{lowered}x on this {_displaySize!.Value.Width}x{_displaySize.Value.Height} screen.";
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

    private void SaveSettings()
    {
        if (_host is not { GameRunning: false } host) return;
        try
        {
            _graphics?.Save();
            var settings = host.Settings;
            (settings.SkipIntro, settings.AsyncReadbacks, settings.PresentViaDxgi, settings.Compat, settings.BuildJobs, settings.Language) =
                (_skipIntro, _asyncReadbacks, _presentViaDxgi, _compat, _buildJobs, _language);
            settings.CommandLineOptions = _commandLineOptions.Trim();
            settings.Save();
            if (_settingsStatus is not null) _settingsStatus.Text = "Saved. Applies at the next start.";
            UpdateSummary();
            AppendLog("Settings saved.");
        }
        catch (Exception error) when (error is IOException or UnauthorizedAccessException)
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
