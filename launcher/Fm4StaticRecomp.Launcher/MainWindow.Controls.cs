// Copyright (c) 2026, i2kiselev
// BSD 3-Clause License: see LICENSE in the repository root.
using System.Globalization;
using Avalonia;
using Avalonia.Automation;
using Avalonia.Controls;
using Avalonia.Controls.Primitives;
using Avalonia.Input;
using Avalonia.Interactivity;
using Avalonia.Layout;
using Avalonia.Media;
using Fm4StaticRecomp.Launcher.Controls;
using Fm4StaticRecomp.Launcher.Core.Settings;

namespace Fm4StaticRecomp.Launcher;

// The CONTROLS panel: the keyboard's keys (driving and menu sets) and the ramp times of
// controls.toml. Its own SAVE; applies at the next start. A click on a key waits for the next key
// press (CaptureKeyDown / CaptureKeyUp); conflicts are shown on the rows, never refused.
public partial class MainWindow
{
    private readonly record struct Capture(ControlSet Set, string Id, int Index);

    private ControlsSettings? _controls;
    private ControlSet _controlSet = ControlSet.Driving;
    // The MOUSE tab beside DRIVING and MENUS.
    private bool _controlsMouse;
    private Capture? _capture;
    private string? _pendingModifier, _controlsProblem;
    private Action? _captureUndo;
    private bool _controlsOtherOpen, _controlsKeysHooked;
    private Button? _saveControlsButton;
    private TextBlock? _controlsUnsaved, _controlsStatus;

    private static readonly Dictionary<string, string> KeyCaps = new()
    {
        ["Return"] = "ENTER", ["Escape"] = "ESC", ["Control"] = "CTRL", ["Backspace"] = "BACKSPACE", ["PageUp"] = "PG UP",
        ["PageDown"] = "PG DN", ["Backtick"] = "`", ["Minus"] = "-", ["Plus"] = "+", ["Comma"] = ",", ["Period"] = ".",
        ["Semicolon"] = ";", ["Slash"] = "/", ["Backslash"] = "\\", ["LBracket"] = "[", ["RBracket"] = "]", ["Quote"] = "'",
        ["NumpadPlus"] = "NUM +", ["NumpadMinus"] = "NUM -", ["NumpadStar"] = "NUM *", ["NumpadSlash"] = "NUM /",
        ["NumpadEnter"] = "NUM ENTER",
    };

    private static string KeyCap(string bind) =>
        string.Join("+", bind.Split('+').Select(part =>
            KeyCaps.TryGetValue(part, out var cap) ? cap
            : part.StartsWith("Numpad", StringComparison.Ordinal) ? "NUM " + part[6..]
            : part.ToUpperInvariant()));

    private void LoadControlsPanel()
    {
        if (!_controlsKeysHooked)
        {
            AddHandler(KeyDownEvent, CaptureKeyDown, RoutingStrategies.Tunnel);
            AddHandler(KeyUpEvent, CaptureKeyUp, RoutingStrategies.Tunnel);
            AddHandler(PointerPressedEvent, (_, _) => AbandonCapture(), RoutingStrategies.Tunnel);
            _controlsKeysHooked = true;
        }
        // Edits made earlier survive switching panels; a saved panel reads the file again. A version
        // that does not read the file (after GO BACK TO) drops them.
        try
        {
            var current = _host?.Controls();
            if (current is null || _controls?.Changed != true) _controls = current;
            _controlsProblem = null;
        }
        catch (Exception error) when (error is IOException or UnauthorizedAccessException)
        {
            _controls = null;
            _controlsProblem = $"controls.toml could not be read: {error.Message}";
        }
        BuildControlsPanel("");
    }

    private void EndControlsCapture()
    {
        _capture = null;
        _pendingModifier = null;
    }

    // Capture swallows every key press, so it must not outlive "PRESS A KEY…" on screen: any click, or
    // the panel turning read-only, ends it and puts the row's own text back without a rebuild (a rebuild
    // would replace the control under the pointer before its click lands).
    private void AbandonCapture()
    {
        if (_capture is null) return;
        _captureUndo?.Invoke();
        _captureUndo = null;
        EndControlsCapture();
        if (_controlsStatus is not null) _controlsStatus.Text = "";
    }

    private void BuildControlsPanel(string status)
    {
        ControlsBody.Children.Clear();
        _captureUndo = null;
        ControlsBody.Children.Add(new VectorHeading { Text = "CONTROLS", FontSize = 24, Foreground = Brush("LabelBrush") });
        _saveControlsButton = null;
        _controlsUnsaved = null;
        _controlsStatus = Help(status);

        if (_host is null || _controls is null)
        {
            ControlsBody.Children.Add(Help(_controlsProblem
                ?? (_host?.Settings.ActiveVersion is null
                    ? "Build the game first."
                    : "CONTROLS needs game version 0.3.2 or later. Update the game to change keys.")));
            return;
        }
        if (_controls.FileIsMalformed)
        {
            ControlsBody.Children.Add(Help("controls.toml is not a list of key = value lines, so the keys cannot be shown. "
                                           + "Edit or delete the file, then open this panel again."));
            return;
        }

        _controlsUnsaved = new TextBlock { Text = "*", Foreground = Brush("AccentBrush"), FontWeight = FontWeight.Bold,
            IsVisible = _controls.Changed };
        _saveControlsButton = new Button
        {
            Content = new StackPanel { Orientation = Orientation.Horizontal, Spacing = 6, Children = { new TextBlock { Text = "SAVE" }, _controlsUnsaved } },
            MinWidth = 120, HorizontalContentAlignment = HorizontalAlignment.Center, Margin = new Thickness(0, 0, 10, 0), Classes = { "quiet" },
        };
        AutomationProperties.SetName(_saveControlsButton, "SAVE");
        _saveControlsButton.Click += (_, _) => SaveControls();
        var resetAll = new Button { Content = "RESET ALL", Classes = { "quiet" } };
        resetAll.Click += (_, _) =>
        {
            EndControlsCapture();
            if (_controlsMouse) _controls.ResetMouseLook();
            else _controls.ResetAll(_controlSet);
            BuildControlsPanel("Defaults shown for this list; SAVE writes them.");
        };
        ControlsBody.Children.Add(new WrapPanel { Children = { _saveControlsButton, resetAll } });
        ControlsBody.Children.Add(_controlsStatus);

        var tabs = new WrapPanel();
        foreach (var (set, label) in new[] { (ControlSet.Driving, "DRIVING"), (ControlSet.Menu, "MENUS") })
        {
            var tab = new Button { Content = label, Classes = { "quiet", "nav" } };
            tab.Classes.Set("selected", !_controlsMouse && set == _controlSet);
            tab.Click += (_, _) =>
            {
                EndControlsCapture();
                _controlSet = set;
                _controlsMouse = false;
                BuildControlsPanel("");
            };
            tabs.Children.Add(tab);
        }
        var mouseTab = new Button { Content = "MOUSE", Classes = { "quiet", "nav" } };
        mouseTab.Classes.Set("selected", _controlsMouse);
        mouseTab.Click += (_, _) =>
        {
            EndControlsCapture();
            _controlsMouse = true;
            BuildControlsPanel("");
        };
        tabs.Children.Add(mouseTab);
        ControlsBody.Children.Add(tabs);
        if (_controlsMouse)
        {
            AddMouseLookRows();
            UpdateControlsActions();
            return;
        }
        ControlsBody.Children.Add(Help(_controlSet == ControlSet.Driving
            ? "Used while the car is on the track and the game is not paused. Each action has a primary key and an optional secondary key."
            : "Used in menus, the pre-race screen, the pause menu, replays and results. Each action has a primary key and an optional secondary key."));

        ControlsBody.Children.Add(Help("The small button names (A, RT, …) are the game's default controller layout. "
                                       + "If you chose another layout in the game's options, an action may be on a different button."));

        var rows = ControlsCatalog.Rows(_controlSet);
        foreach (var row in rows.Where(r => !r.Other)) ControlsBody.Children.Add(ControlRowView(row));
        var other = rows.Where(r => r.Other).ToList();
        var toggle = new Button { Content = (_controlsOtherOpen ? "HIDE" : "SHOW") + $" OTHER BUTTONS ({other.Count})", Classes = { "link" }, FontSize = 16,
            Padding = new Thickness(0, 10), HorizontalAlignment = HorizontalAlignment.Left };
        toggle.Click += (_, _) =>
        {
            _controlsOtherOpen = !_controlsOtherOpen;
            BuildControlsPanel("");
        };
        ControlsBody.Children.Add(toggle);
        if (_controlsOtherOpen)
            foreach (var row in other) ControlsBody.Children.Add(ControlRowView(row));

        AddRampRows();
        UpdateControlsActions();
    }

    private void AddMouseLookRows()
    {
        ControlsBody.Children.Add(Help("In a race, hold the right mouse button and move the mouse left or right to look around the car. "
                                       + "Let go and the camera goes back behind it."));
        const string lookLabel = "Mouse look (hold right button)", sensitivityLabel = "Mouse look sensitivity";
        var box = new CheckBox { IsChecked = _controls!.MouseLook, Content = new TextBlock { Text = lookLabel } };
        var slider = new Slider
        {
            Minimum = ControlsSettings.MouseLookLow, Maximum = ControlsSettings.MouseLookHigh, Value = _controls.MouseLookSensitivity,
            SmallChange = 1, LargeChange = 5, IsSnapToTickEnabled = true, TickFrequency = 1, Width = 320,
        };
        var value = new TextBlock { Width = 64, VerticalAlignment = VerticalAlignment.Center, Foreground = Brush("ValueBrush") };
        var reset = new Button { Content = "RESET", Classes = { "link" }, FontSize = 16, Padding = new Thickness(14, 10), VerticalAlignment = VerticalAlignment.Center };
        void Show()
        {
            value.Text = _controls.MouseLookSensitivity.ToString(CultureInfo.InvariantCulture);
            reset.IsEnabled = !_controls.MouseLookIsDefault;
            if (_controlsUnsaved is not null) _controlsUnsaved.IsVisible = _controls.Changed;
        }
        box.IsCheckedChanged += (_, _) =>
        {
            _controls.MouseLook = box.IsChecked == true;
            Show();
        };
        slider.ValueChanged += (_, e) =>
        {
            _controls.SetMouseLookSensitivity((int)Math.Round(e.NewValue));
            Show();
        };
        reset.Click += (_, _) =>
        {
            _controls.ResetMouseLook();
            box.IsChecked = _controls.MouseLook;
            slider.Value = _controls.MouseLookSensitivity;
            Show();
        };
        AutomationProperties.SetName(box, lookLabel);
        AutomationProperties.SetName(slider, sensitivityLabel);
        Show();
        ControlsBody.Children.Add(box);
        var line = new StackPanel { Orientation = Orientation.Horizontal, Spacing = 10, Children = { slider, value, reset } };
        var grid = new Grid { ColumnDefinitions = new ColumnDefinitions("260,*") };
        grid.Children.Add(new TextBlock { Text = sensitivityLabel, VerticalAlignment = VerticalAlignment.Center });
        Grid.SetColumn(line, 1);
        grid.Children.Add(line);
        ControlsBody.Children.Add(grid);
    }

    private Control ControlRowView(ControlRow row)
    {
        var set = _controlSet;
        var keys = _controls!.Keys(set, row.Id);
        var taken = _controls.TakenFrom(set, row.Id);
        var line = new Grid { ColumnDefinitions = new ColumnDefinitions("260,*,Auto") };
        line.Children.Add(new StackPanel
        {
            VerticalAlignment = VerticalAlignment.Center,
            Children = { new TextBlock { Text = row.Label }, new TextBlock { Text = row.Pad, Classes = { "help" } } },
        });

        // A hand-edited file may list more than two keys: each one is shown.
        var caps = new WrapPanel { VerticalAlignment = VerticalAlignment.Center };
        Grid.SetColumn(caps, 1);
        for (var slot = 0; slot < Math.Max(keys.Count, ControlsSettings.MaxKeys); slot++)
        {
            var index = slot;
            var empty = slot >= keys.Count;
            var capturing = _capture == new Capture(set, row.Id, empty ? -1 : slot);
            var idle = empty || taken.Lost.Contains(keys[slot]) ? Brush("HelpBrush") : null;
            var text = empty ? "NOT SET" : KeyCap(keys[slot]);
            var cap = new Button { Content = capturing ? "PRESS A KEY…" : text, MinWidth = 96, Margin = new Thickness(0, 0, 4, 4), Classes = { "quiet" } };
            if (capturing)
            {
                cap.Foreground = Brush("AccentBrush");
                _captureUndo += () =>
                {
                    cap.Content = text;
                    if (idle is null) cap.ClearValue(TemplatedControl.ForegroundProperty);
                    else cap.Foreground = idle;
                };
            }
            else if (idle is not null) cap.Foreground = idle;
            var slotName = slot switch { 0 => "primary", 1 => "secondary", _ => "extra" };
            AutomationProperties.SetName(cap, capturing ? $"{row.Label}: waiting for a key"
                : empty ? $"{row.Label}: set the {slotName} key" : $"{row.Label}: {slotName} key {text}");
            cap.Click += (_, _) => StartCapture(new Capture(set, row.Id, empty ? -1 : index));
            caps.Children.Add(cap);
            if (slot > 0 && !empty)
            {
                var remove = new Button
                {
                    Content = "✕", Classes = { "quiet" }, FontSize = 17, MinWidth = 42, Padding = new Thickness(0, 10),
                    Margin = new Thickness(0, 0, 10, 4), VerticalAlignment = VerticalAlignment.Top, HorizontalContentAlignment = HorizontalAlignment.Center,
                };
                AutomationProperties.SetName(remove, $"Remove {text} from {row.Label}");
                remove.Click += (_, _) =>
                {
                    EndControlsCapture();
                    _controls.RemoveKey(set, row.Id, index);
                    BuildControlsPanel("");
                };
                caps.Children.Add(remove);
            }
        }
        if (_capture is { } active && active.Set == set && active.Id == row.Id)
        {
            var cancel = new Button { Content = "CANCEL", Classes = { "quiet" }, Margin = new Thickness(0, 0, 4, 4) };
            cancel.Click += (_, _) =>
            {
                EndControlsCapture();
                BuildControlsPanel("");
            };
            caps.Children.Add(cancel);
            _captureUndo += () => cancel.IsVisible = false;
        }
        line.Children.Add(caps);

        var reset = new Button { Content = "RESET", Classes = { "link" }, FontSize = 16, Padding = new Thickness(14, 10), VerticalAlignment = VerticalAlignment.Center,
            IsEnabled = !_controls.IsDefault(set, row.Id) };
        AutomationProperties.SetName(reset, $"Reset {row.Label}");
        reset.Click += (_, _) =>
        {
            EndControlsCapture();
            _controls.Reset(set, row.Id);
            BuildControlsPanel("");
        };
        Grid.SetColumn(reset, 2);
        line.Children.Add(reset);

        var column = new StackPanel { Spacing = 0, Children = { line } };
        if (_controls.ConflictsWith(set, row.Id) is { Count: > 0 } conflicts)
            column.Children.Add(new TextBlock { Text = "Also used by: " + string.Join(", ", conflicts.Select(label =>
                    !_controlsOtherOpen && ControlsCatalog.Rows(set).Any(r => r.Other && r.Label == label) ? $"{label} (under OTHER BUTTONS)" : label)),
                Foreground = Brush("AccentBrush"),
                FontSize = 13, Margin = new Thickness(260, 0, 0, 2), TextWrapping = TextWrapping.Wrap });
        if (taken.By.Count > 0)
            column.Children.Add(new TextBlock
            {
                Text = $"{string.Join(", ", taken.Lost.Select(KeyCap))} taken by {string.Join(", ", taken.By)}; "
                       + (taken.Kept.Count > 0 ? $"this action keeps {string.Join(", ", taken.Kept.Select(KeyCap))}." : "the game gives this action another key when it starts."),
                Foreground = Brush("HelpBrush"), FontSize = 13, Margin = new Thickness(260, 0, 0, 2), TextWrapping = TextWrapping.Wrap,
            });
        return column;
    }

    private void AddRampRows()
    {
        ControlsBody.Children.Add(new TextBlock { Text = "SMOOTHING", Classes = { "header" }, Margin = new Thickness(0, 12, 0, 0) });
        ControlsBody.Children.Add(Help("Seconds a key takes to move steering, the pedals and looking around to full, and back to rest "
                                       + "when released. Lower is snappier."));
        foreach (var ramp in ControlsCatalog.Ramps)
        {
            var slider = new Slider
            {
                Minimum = ControlsSettings.RampLow, Maximum = ControlsSettings.RampHigh, Value = _controls!.Ramp(ramp.Key),
                SmallChange = 0.01, LargeChange = 0.05, IsSnapToTickEnabled = true, TickFrequency = 0.01, Width = 320,
            };
            var value = new TextBlock { Width = 64, VerticalAlignment = VerticalAlignment.Center, Foreground = Brush("ValueBrush") };
            var reset = new Button { Content = "RESET", Classes = { "link" }, FontSize = 16, Padding = new Thickness(14, 10), VerticalAlignment = VerticalAlignment.Center };
            void Show()
            {
                value.Text = _controls.Ramp(ramp.Key).ToString("0.00", CultureInfo.InvariantCulture) + " s";
                reset.IsEnabled = !_controls.RampIsDefault(ramp.Key);
                if (_controlsUnsaved is not null) _controlsUnsaved.IsVisible = _controls.Changed;
            }
            slider.ValueChanged += (_, e) =>
            {
                _controls.SetRamp(ramp.Key, e.NewValue);
                Show();
            };
            reset.Click += (_, _) =>
            {
                _controls.ResetRamp(ramp.Key);
                slider.Value = _controls.Ramp(ramp.Key);
                Show();
            };
            AutomationProperties.SetName(slider, ramp.Label);
            Show();
            var line = new StackPanel { Orientation = Orientation.Horizontal, Spacing = 10, Children = { slider, value, reset } };
            var grid = new Grid { ColumnDefinitions = new ColumnDefinitions("260,*") };
            grid.Children.Add(new TextBlock { Text = ramp.Label, VerticalAlignment = VerticalAlignment.Center });
            Grid.SetColumn(line, 1);
            grid.Children.Add(line);
            ControlsBody.Children.Add(grid);
        }
    }

    private void StartCapture(Capture target)
    {
        _capture = target;
        _pendingModifier = null;
        BuildControlsPanel("Press the key you want. Esc is a key like any other; CANCEL backs out.");
    }

    private void CaptureKeyDown(object? sender, KeyEventArgs e)
    {
        if (_capture is null) return;
        e.Handled = true;
        var name = KeyNames.FromAvalonia(e.Key.ToString());
        if (name is null)
        {
            if (_controlsStatus is not null) _controlsStatus.Text = "The game cannot use that key. Press another.";
            return;
        }
        // A modifier is held before the key it modifies, so it binds on its own release if no other key came meanwhile.
        if (KeyNames.IsModifier(name)) _pendingModifier = name;
        else BindCaptured(name);
    }

    private void CaptureKeyUp(object? sender, KeyEventArgs e)
    {
        if (_capture is null || _pendingModifier is not { } modifier) return;
        e.Handled = true;
        BindCaptured(modifier);
    }

    private void BindCaptured(string key)
    {
        if (_capture is not { } target || _controls is null) return;
        EndControlsCapture();
        var status = "";
        if (target.Index < 0)
        {
            var before = _controls.Keys(target.Set, target.Id).Count;
            _controls.AddKey(target.Set, target.Id, key);
            if (_controls.Keys(target.Set, target.Id).Count == before) status = "That key is already on this action.";
        }
        else _controls.SetKey(target.Set, target.Id, target.Index, key);
        BuildControlsPanel(status);
    }

    private void SaveControls()
    {
        if (_host is not { GameRunning: false } || _controls is null) return;
        string status;
        try
        {
            _controls.Save();
            status = "Saved. Applies at the next start.";
            AppendLog("Controls saved.");
        }
        catch (Exception error) when (error is IOException or UnauthorizedAccessException)
        {
            status = $"Not saved: {error.Message}";
        }
        EndControlsCapture();
        BuildControlsPanel(status);
    }

    private void UpdateControlsActions()
    {
        ControlsBody.IsEnabled = _host is { GameRunning: false } && !_busy;
        if (!ControlsBody.IsEnabled) AbandonCapture();
    }
}
