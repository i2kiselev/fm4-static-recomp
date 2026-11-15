// Copyright (c) 2026, FugaziScript
// BSD 3-Clause License: see LICENSE in the repository root.
using System.Globalization;
using Avalonia;
using Avalonia.Automation;
using Avalonia.Controls;
using Avalonia.Layout;
using Avalonia.Media;
using Avalonia.Threading;
using Fm4StaticRecomp.Launcher.Controls;
using Fm4StaticRecomp.Launcher.Core.Launch;
using Fm4StaticRecomp.Launcher.Core.Settings;

namespace Fm4StaticRecomp.Launcher;

// The WHEEL panel: a racing wheel built from any USB devices (wheel base, pedals, shifter, button
// box) as player one, and the game's own force feedback (src\wheel). Every change is written to
// config\wheel.toml at once and applies at the next start. While the panel is open the game runs
// as --fm4_wheel_probe, so the live view reads the controllers through the game's own SDL and maths.
public partial class MainWindow
{
    private sealed record WheelBind(bool Axis, Action<string> Apply);

    private WheelSettings? _wheel;
    private WheelProbe? _wheelProbe;
    private WheelSnapshot? _wheelSnapshot;
    private WheelCapture? _wheelCapture;
    private WheelBind? _wheelBind;
    private DateTime _wheelCaptureEnds;
    private string _wheelDevicesShown = "";
    private TextBlock? _wheelStatus, _wheelForceStatus, _wheelDevicesText;
    private readonly Dictionary<string, TextBlock> _wheelButtonLabels = [];
    private ProgressBar? _wheelSteering, _wheelThrottle, _wheelBrake, _wheelClutch;

    private static readonly string[] FfbModes = ["on", "rumble", "off"];
    private static readonly string[] FfbModeLabels = ["GAME'S FORCES", "RUMBLE ONLY", "OFF"];

    // Pad button bits (rex/input/input.h), for the live view.
    private static readonly (int Bit, string Name)[] PadBits =
    [
        (0x1000, "A"), (0x2000, "B"), (0x4000, "X"), (0x8000, "Y"), (0x0100, "LB"), (0x0200, "RB"),
        (0x0040, "LS"), (0x0080, "RS"), (0x0020, "BACK"), (0x0010, "START"),
        (0x0001, "UP"), (0x0002, "DOWN"), (0x0004, "LEFT"), (0x0008, "RIGHT"),
    ];

    private void LoadWheelPanel()
    {
        _wheel = _host?.Wheel();
        StartWheelProbe();
        BuildWheelPanel("");
    }

    private void StartWheelProbe()
    {
        if (_wheelProbe is not null || _host is not { GameRunning: false } host) return;
        if (host.WheelProbeExecutable() is not { } executable || host.Settings.ActiveVersion is not { } version) return;
        try
        {
            var probe = WheelProbe.Start(executable, host.Layout.BuildFolder(version), LaunchArguments.ForWheelProbe(host.Layout));
            probe.Updated += snapshot => Dispatcher.UIThread.Post(() => OnWheelSnapshot(probe, snapshot));
            probe.Exited += () => Dispatcher.UIThread.Post(() =>
            {
                if (_wheelProbe != probe) return;
                StopWheelProbe();
                ShowWheelStatus("The live view stopped. Open the panel again to restart it.");
            });
            _wheelProbe = probe;
        }
        catch (Exception error) when (error is System.ComponentModel.Win32Exception or IOException or InvalidOperationException)
        {
            _wheelProbe = null;
        }
    }

    private void StopWheelProbe()
    {
        var probe = _wheelProbe;
        _wheelProbe = null;
        _wheelSnapshot = null;
        _wheelCapture = null;
        _wheelBind = null;
        _wheelDevicesShown = "";
        probe?.Dispose();
    }

    private void OnWheelSnapshot(WheelProbe probe, WheelSnapshot snapshot)
    {
        if (probe != _wheelProbe || _panel != Panel.Wheel) return;
        _wheelSnapshot = snapshot;
        if (_wheelCapture is not null && _wheelBind is not null)
        {
            if (_wheelCapture.Next(snapshot) is { } binding)
            {
                var bind = _wheelBind;
                _wheelCapture = null;
                _wheelBind = null;
                bind.Apply(binding);
                BuildWheelPanel($"Bound {binding}.");
                return;
            }
            if (DateTime.UtcNow > _wheelCaptureEnds)
            {
                _wheelCapture = null;
                _wheelBind = null;
                BuildWheelPanel("Nothing moved; nothing was bound.");
                return;
            }
        }
        if (DevicesKey(snapshot) != _wheelDevicesShown && _wheelCapture is null)
        {
            BuildWheelPanel(_wheelStatus?.Text ?? "");
            return;
        }
        UpdateWheelLive(snapshot);
    }

    // What the panel's layout depends on: a change rebuilds it.
    private static string DevicesKey(WheelSnapshot? snapshot) => snapshot is null ? ""
        : $"{snapshot.Ready}\n{snapshot.Base}\n{string.Join("\n", snapshot.Devices.Select(d => d.Match))}";

    private void UpdateWheelLive(WheelSnapshot snapshot)
    {
        if (_wheelSteering is not null) _wheelSteering.Value = snapshot.Pad.Steering;
        if (_wheelThrottle is not null) _wheelThrottle.Value = snapshot.Pad.Throttle;
        if (_wheelBrake is not null) _wheelBrake.Value = snapshot.Pad.Brake;
        if (_wheelClutch is not null) _wheelClutch.Value = snapshot.Pad.Clutch;
        // The controls held now light up in BUTTON BINDINGS.
        var held = PadBits.Where(b => (snapshot.Pad.Buttons & b.Bit) != 0).Select(b => b.Name).ToHashSet();
        if (snapshot.Pad.Gear != 0) held.Add(snapshot.Pad.Gear < 0 ? "REVERSE" : $"GEAR{snapshot.Pad.Gear}");
        foreach (var (name, label) in _wheelButtonLabels)
            label.Foreground = held.Contains(name) ? Brush("AccentBrush") : Brush("LabelBrush");
        if (_wheelForceStatus is not null)
            _wheelForceStatus.Text = snapshot.Status.Length > 0 ? $"Force feedback: {snapshot.Status}." : "";
    }

    // Which collapsible parts are open, kept across the panel's rebuilds.
    private bool _wheelButtonsOpen, _wheelDevicesOpen, _wheelEffectsOpen;

    private void BuildWheelPanel(string status)
    {
        WheelBody.Children.Clear();
        _wheelSteering = _wheelThrottle = _wheelBrake = _wheelClutch = null;
        _wheelForceStatus = _wheelDevicesText = null;
        _wheelButtonLabels.Clear();
        WheelBody.Children.Add(new VectorHeading { Text = "WHEEL", FontSize = 24, Foreground = Brush("LabelBrush") });
        _wheelStatus = Help(status);
        _wheelStatus.IsVisible = status.Length > 0;
        if (_host is null || _wheel is null)
        {
            WheelBody.Children.Add(Help("Build the game first."));
            return;
        }
        if (_wheel.FileIsMalformed)
        {
            WheelBody.Children.Add(Help("wheel.toml is not a list of key = value lines. Edit or delete the file, then open this panel again."));
            return;
        }
        var wheel = _wheel;
        var snapshot = _wheelSnapshot;
        _wheelDevicesShown = DevicesKey(snapshot);

        WheelBody.Children.Add(Help("A racing wheel as player one; the wheel base, pedals, shifter and button box can be separate "
            + "USB devices. Changes are saved at once and apply the next time the game starts. Set the wheel's rotation in its "
            + "own driver (Thrustmaster, Fanatec, Logitech…): full lock in the game comes at the wheel's full rotation."));
        WheelBody.Children.Add(WheelCheck("ENABLE WHEEL SUPPORT", "enabled"));
        if (_wheelProbe is null)
            WheelBody.Children.Add(Help(_host.GameRunning
                ? "The live view, BIND and TEST wait for the game to close."
                : "This build of the game has no live view: rebuild it to bind here. The settings below still apply."));
        else if (snapshot is null || !snapshot.Ready)
            WheelBody.Children.Add(Help("Looking for controllers…"));
        WheelBody.Children.Add(_wheelStatus);

        AddWheelInputs(wheel, snapshot);
        AddWheelButtons(wheel);
        AddWheelDevices(wheel, snapshot);
        AddWheelForce(wheel, snapshot);
    }

    // WHAT THE GAME READS: each axis live, with its binding, BIND and INVERT on the same row.
    private void AddWheelInputs(WheelSettings wheel, WheelSnapshot? snapshot)
    {
        WheelBody.Children.Add(Header("WHAT THE GAME READS"));
        WheelBody.Children.Add(Help("BIND, then turn the wheel a quarter turn or press the pedal fully. INVERT a control that "
            + "reads backwards; a pedal on another USB device is bound with its name."));
        ProgressBar Bar(double minimum, double maximum) => new()
        {
            Minimum = minimum, Maximum = maximum, Height = 8, Width = 200, CornerRadius = new CornerRadius(0),
            Foreground = Brush("AccentBrush"), Background = Brush("SliderLowBrush"), VerticalAlignment = VerticalAlignment.Center,
        };
        _wheelSteering = Bar(-32768, 32767);
        _wheelThrottle = Bar(0, 255);
        _wheelBrake = Bar(0, 255);
        _wheelClutch = Bar(0, 255);

        var steering = wheel.Get("device") is { Length: > 0 } device
            ? $"{device}|axis{wheel.GetInt("steering_axis")}" : $"axis{wheel.GetInt("steering_axis")}";
        var invertSteering = WheelInvert("STEERING", wheel.GetBool("steering_invert"), on => SetWheel("steering_invert", on ? "true" : "false"));
        WheelBody.Children.Add(InputRow("STEERING", _wheelSteering, steering, binding =>
        {
            // Steering names the wheel base: the device it is on becomes the base.
            if (WheelCapture.AxisIndex(binding) is not { } index) return;
            var on = WheelCapture.DeviceOf(binding);
            if (on.Length > 0 || snapshot?.Base is { Length: > 0 }) SetWheel("device", on.Length > 0 ? on : snapshot!.Base);
            SetWheel("steering_axis", index.ToString(CultureInfo.InvariantCulture));
        }, invertSteering, clear: null));

        foreach (var (label, key, bar) in new[] { ("THROTTLE", "throttle", _wheelThrottle), ("BRAKE", "brake", _wheelBrake),
                     ("CLUTCH", "clutch", _wheelClutch) })
        {
            var current = wheel.Get(key);
            CheckBox? invert = null;
            if (WheelCapture.AxisIndex(current) is not null)
            {
                var rest = PedalRest(current);
                invert = WheelInvert(label, WheelCapture.IsReversed(current, rest), _ =>
                {
                    SetWheel(key, WheelCapture.Reverse(current, rest));
                    BuildWheelPanel("");
                });
            }
            WheelBody.Children.Add(InputRow(label, bar, current, binding => SetWheel(key, binding), invert,
                clear: () => SetWheel(key, "")));
        }

        WheelBody.Children.Add(WheelSlider("STEERING DEAD ZONE", "steering_deadzone", 0, 20, "%"));
        WheelBody.Children.Add(WheelSlider("PEDAL DEAD ZONE", "pedal_deadzone", 0, 20, "%"));
        var clutch = new ComboBox { ItemsSource = WheelSettings.ButtonNames.Take(16).ToList(), Width = 120 };
        clutch.SelectedItem = wheel.Get("clutch_button");
        clutch.SelectionChanged += (_, _) =>
        {
            if (clutch.SelectedItem is string name) SetWheel("clutch_button", name);
        };
        AutomationProperties.SetName(clutch, "CLUTCH PRESSES");
        WheelBody.Children.Add(Row("CLUTCH PRESSES", clutch));
        WheelBody.Children.Add(Help("FM4's clutch is a button: past half travel the clutch pedal presses the one above. "
            + "Choose MANUAL WITH CLUTCH in the game and give its clutch that button."));
        if (snapshot is not null) UpdateWheelLive(snapshot);
    }

    private void AddWheelButtons(WheelSettings wheel)
    {
        var body = new StackPanel { Spacing = 8 };
        body.Children.Add(Help("BIND, then press a button, push the hat or pull a lever. ADD binds one more input to the same "
            + "control, CLEAR removes them all. A control lights up while you hold it. GEAR 1 to 7 and REVERSE are an "
            + "H-shifter's gates."));
        var buttons = wheel.Buttons();
        foreach (var name in WheelSettings.ButtonNames)
        {
            var inputs = buttons.Where(b => b.Name == name).Select(b => b.Input).ToList();
            var row = new StackPanel { Orientation = Orientation.Horizontal, Spacing = 8 };
            row.Children.Add(BindingText(inputs.Count > 0 ? string.Join(", ", inputs) : ""));
            row.Children.Add(BindButton("BIND", false, binding => wheel.BindButton(name, binding, add: false)));
            if (inputs.Count > 0)
            {
                row.Children.Add(BindButton("ADD", false, binding => wheel.BindButton(name, binding, add: true)));
                row.Children.Add(ClearButton(() =>
                {
                    wheel.ClearButton(name);
                    _wheelProbe?.Set("buttons", wheel.Get("buttons"));
                }));
            }
            var label = new TextBlock { Text = name.StartsWith("GEAR", StringComparison.Ordinal) ? "GEAR " + name[4..] : name,
                VerticalAlignment = VerticalAlignment.Center, Foreground = Brush("LabelBrush") };
            _wheelButtonLabels[name] = label;
            body.Children.Add(Row(label, row));
        }
        WheelBody.Children.Add(Section("BUTTON BINDINGS", body, _wheelButtonsOpen, open => _wheelButtonsOpen = open));
    }

    private void AddWheelDevices(WheelSettings wheel, WheelSnapshot? snapshot)
    {
        var body = new StackPanel { Spacing = 6 };
        if (snapshot is null)
            body.Children.Add(Help(wheel.Get("device") is { Length: > 0 } device ? $"Wheel base: {device}" : "Wheel base: the first wheel found."));
        else
        {
            var lines = snapshot.Devices.Select(d => $"{d.Name} ({d.Ids}){(d.Match == snapshot.Base ? " — WHEEL BASE" : "")}").ToList();
            _wheelDevicesText = new TextBlock { Text = lines.Count > 0 ? string.Join("\n", lines) : "No controllers connected.",
                Foreground = Brush("ValueBrush"), TextWrapping = TextWrapping.Wrap };
            body.Children.Add(_wheelDevicesText);
            if (wheel.Get("device").Length > 0 && snapshot.Base.Length == 0)
                body.Children.Add(Help($"The wheel base {wheel.Get("device")} is not connected."));
        }
        WheelBody.Children.Add(Section("DEVICES", body, _wheelDevicesOpen, open => _wheelDevicesOpen = open));
    }

    private void AddWheelForce(WheelSettings wheel, WheelSnapshot? snapshot)
    {
        WheelBody.Children.Add(Header("FORCE FEEDBACK"));
        WheelBody.Children.Add(Help("TEST pulls the wheel left, then right: tick INVERT if it pulled right first. TEST RUMBLE "
            + "plays the engine's buzz, then the road's rumble, at the RUMBLE strength under EFFECTS. Start with a low "
            + "STRENGTH on a direct-drive base; above 100% the strongest forces clip. Some bases appear twice in FORCE DEVICE: "
            + "if TEST does not move the wheel, pick the other entry."));
        var mode = new ComboBox { ItemsSource = FfbModeLabels, Width = 220,
            SelectedIndex = Math.Max(0, Array.IndexOf(FfbModes, wheel.Get("ffb_mode"))) };
        mode.SelectionChanged += (_, _) =>
        {
            if (mode.SelectedIndex >= 0) SetWheel("ffb_mode", FfbModes[mode.SelectedIndex]);
        };
        AutomationProperties.SetName(mode, "FORCES");
        WheelBody.Children.Add(Row("FORCES", mode));
        if (snapshot is not null)
        {
            var choices = new List<string> { "THE WHEEL BASE" };
            choices.AddRange(snapshot.Devices.Select(d => $"{d.Name} ({d.Ids})"));
            var device = new ComboBox { ItemsSource = choices, Width = 320 };
            var current = wheel.Get("ffb_device");
            var index = snapshot.Devices.ToList().FindIndex(d => d.Match == current);
            device.SelectedIndex = current.Length == 0 ? 0 : index >= 0 ? index + 1 : -1;
            device.SelectionChanged += (_, _) =>
            {
                if (device.SelectedIndex >= 0)
                    SetWheel("ffb_device", device.SelectedIndex == 0 ? "" : snapshot.Devices[device.SelectedIndex - 1].Match);
            };
            AutomationProperties.SetName(device, "FORCE DEVICE");
            WheelBody.Children.Add(Row("FORCE DEVICE", device));
        }
        WheelBody.Children.Add(WheelSlider("STRENGTH", "ffb_strength", 0, 200, "%"));
        var test = new Button { Content = "TEST", Classes = { "quiet" }, IsEnabled = _wheelProbe is not null && snapshot is { TestBusy: false },
            VerticalAlignment = VerticalAlignment.Center };
        test.Click += (_, _) =>
        {
            _wheelProbe?.Test();
            if (_wheelForceStatus is not null) _wheelForceStatus.Text = "Force feedback: testing…";
        };
        _wheelForceStatus = Help("");
        _wheelForceStatus.VerticalAlignment = VerticalAlignment.Center;
        var invert = WheelInvert("FORCE", wheel.GetBool("ffb_invert"), on => SetWheel("ffb_invert", on ? "true" : "false"));
        var rumble = new Button { Content = "TEST RUMBLE", Classes = { "quiet" },
            IsEnabled = _wheelProbe is not null && snapshot is { TestBusy: false }, VerticalAlignment = VerticalAlignment.Center };
        rumble.Click += (_, _) =>
        {
            _wheelProbe?.TestRumble();
            if (_wheelForceStatus is not null) _wheelForceStatus.Text = "Force feedback: testing the rumble…";
        };
        WheelBody.Children.Add(Row("TEST", new StackPanel { Orientation = Orientation.Horizontal, Spacing = 12,
            Children = { test, rumble, invert, _wheelForceStatus } }));
        if (snapshot is not null) UpdateWheelLive(snapshot);

        var effects = new StackPanel { Spacing = 8 };
        effects.Children.Add(Help("The share of each of the game's effects, the smallest force a geared or belted wheel still "
            + "feels, and the game's rumble (engine and road) played as a vibration."));
        effects.Children.Add(WheelSlider("CENTRING SPRING", "ffb_spring", 0, 200, "%"));
        effects.Children.Add(WheelSlider("DAMPER", "ffb_damper", 0, 200, "%"));
        effects.Children.Add(WheelSlider("FRICTION", "ffb_friction", 0, 200, "%"));
        effects.Children.Add(WheelSlider("INERTIA", "ffb_inertia", 0, 200, "%"));
        effects.Children.Add(WheelSlider("MINIMUM FORCE", "ffb_min_force", 0, 30, "%"));
        effects.Children.Add(WheelSlider("RUMBLE", "ffb_rumble", 0, 100, "%"));
        WheelBody.Children.Add(Section("EFFECTS", effects, _wheelEffectsOpen, open => _wheelEffectsOpen = open));
    }

    // The resting value of a pedal binding's axis, from the live view; null while unknown.
    private int? PedalRest(string binding)
    {
        if (_wheelSnapshot is not { } snapshot || WheelCapture.AxisIndex(binding) is not { } index) return null;
        var device = WheelCapture.DeviceOf(binding) is { Length: > 0 } named ? named : snapshot.Base;
        // Bindings written by hand may name a device by part of its name or by its ids.
        var on = snapshot.Devices.FirstOrDefault(d => d.Match == device)
                 ?? snapshot.Devices.FirstOrDefault(d => d.Ids.Equals(device, StringComparison.OrdinalIgnoreCase)
                                                         || d.Name.Contains(device, StringComparison.OrdinalIgnoreCase));
        return on is not null && index < on.Rest.Count ? on.Rest[index] : null;
    }

    // A label left and the control right, without a description.
    private static Control Row(string label, Control control) =>
        Row(new TextBlock { Text = label, VerticalAlignment = VerticalAlignment.Center }, control);

    private static Control Row(TextBlock label, Control control)
    {
        var grid = new Grid { ColumnDefinitions = new ColumnDefinitions("170,*") };
        grid.Children.Add(label);
        Grid.SetColumn(control, 1);
        grid.Children.Add(control);
        return grid;
    }

    // A collapsible part of the panel.
    private static Control Section(string title, Control body, bool open, Action<bool> remember)
    {
        var expander = new Expander
        {
            Header = new TextBlock { Text = title, Classes = { "header" } }, Content = body, IsExpanded = open,
            HorizontalAlignment = HorizontalAlignment.Stretch, HorizontalContentAlignment = HorizontalAlignment.Stretch,
            Margin = new Thickness(0, 8, 0, 0),
        };
        expander.Expanding += (_, _) => remember(true);
        expander.Collapsing += (_, _) => remember(false);
        return expander;
    }

    // An axis row: its live bar, its binding, BIND, INVERT and, for a bound pedal, CLEAR.
    private Control InputRow(string label, ProgressBar bar, string binding, Action<string> apply, CheckBox? invert, Action? clear)
    {
        var row = new StackPanel { Orientation = Orientation.Horizontal, Spacing = 10,
            Children = { bar, BindingText(binding), BindButton("BIND", true, apply) } };
        if (invert is not null) row.Children.Add(invert);
        if (clear is not null && binding.Length > 0) row.Children.Add(ClearButton(clear));
        return Row(label, row);
    }

    private TextBlock BindingText(string binding) => new()
    {
        Text = binding.Length > 0 ? binding : "—", Foreground = Brush("ValueBrush"), VerticalAlignment = VerticalAlignment.Center,
        Width = 170, TextTrimming = TextTrimming.CharacterEllipsis,
    };

    private Button ClearButton(Action clear)
    {
        var button = new Button { Content = "CLEAR", Classes = { "quiet" }, VerticalAlignment = VerticalAlignment.Center };
        AutomationProperties.SetName(button, "CLEAR");
        button.Click += (_, _) =>
        {
            clear();
            BuildWheelPanel("");
        };
        return button;
    }

    private static CheckBox WheelInvert(string label, bool value, Action<bool> changed)
    {
        var box = new CheckBox { Content = new TextBlock { Text = "INVERT" }, IsChecked = value, VerticalAlignment = VerticalAlignment.Center };
        AutomationProperties.SetName(box, $"INVERT {label}");
        box.IsCheckedChanged += (_, _) => changed(box.IsChecked == true);
        return box;
    }

    private Button BindButton(string text, bool axis, Action<string> apply)
    {
        var button = new Button { Content = text, Classes = { "quiet" }, IsEnabled = _wheelProbe is not null && _wheelSnapshot is not null,
            VerticalAlignment = VerticalAlignment.Center };
        AutomationProperties.SetName(button, text);
        button.Click += (_, _) =>
        {
            if (_wheelSnapshot is not { } start) return;
            _wheelCapture = new WheelCapture(start, axis);
            _wheelBind = new WheelBind(axis, binding =>
            {
                apply(binding);
                // Button rows write through WheelSettings.BindButton.
                if (_wheel is { } wheel) _wheelProbe?.Set("buttons", wheel.Get("buttons"));
            });
            _wheelCaptureEnds = DateTime.UtcNow.AddSeconds(8);
            button.Content = axis ? "MOVE IT…" : "PRESS…";
        };
        return button;
    }

    private void ShowWheelStatus(string text)
    {
        if (_wheelStatus is null) return;
        _wheelStatus.Text = text;
        _wheelStatus.IsVisible = text.Length > 0;
    }

    private void SetWheel(string key, string value)
    {
        if (_wheel is null) return;
        try
        {
            _wheel.Set(key, value);
            _wheelProbe?.Set(key, value);
        }
        catch (Exception error) when (error is IOException or UnauthorizedAccessException)
        {
            ShowWheelStatus($"wheel.toml could not be written: {error.Message}");
        }
    }

    private Control WheelCheck(string label, string key)
    {
        var box = new CheckBox { IsChecked = _wheel?.GetBool(key) == true, Content = new TextBlock { Text = label } };
        AutomationProperties.SetName(box, label);
        box.IsCheckedChanged += (_, _) => SetWheel(key, box.IsChecked == true ? "true" : "false");
        return box;
    }

    private Control WheelSlider(string label, string key, int minimum, int maximum, string unit)
    {
        var value = Math.Clamp(_wheel?.GetInt(key) ?? minimum, minimum, maximum);
        var slider = new Slider { Minimum = minimum, Maximum = maximum, Value = value, SmallChange = 1, LargeChange = 10,
            IsSnapToTickEnabled = true, TickFrequency = 1, Width = 260 };
        var shown = new TextBlock { Text = $"{value}{unit}", Width = 56, VerticalAlignment = VerticalAlignment.Center,
            Foreground = Brush("ValueBrush") };
        slider.ValueChanged += (_, e) =>
        {
            var now = (int)Math.Round(e.NewValue);
            shown.Text = $"{now}{unit}";
            SetWheel(key, now.ToString(CultureInfo.InvariantCulture));
        };
        AutomationProperties.SetName(slider, label);
        return Row(label, new StackPanel { Orientation = Orientation.Horizontal, Spacing = 10, Children = { slider, shown } });
    }
}
