// Copyright (c) 2026, i2kiselev
// BSD 3-Clause License: see LICENSE in the repository root.
using System.Globalization;
using Avalonia;
using Avalonia.Automation;
using Avalonia.Controls;
using Avalonia.Layout;
using Avalonia.Media;
using Avalonia.Threading;
using Fm4StaticRecomp.Launcher.Controls;
using Fm4StaticRecomp.Launcher.Core.Settings;
using Fm4StaticRecomp.Launcher.Core.Wheel;

namespace Fm4StaticRecomp.Launcher;

// The WHEEL panel: wheel.toml's bindings (SETUP, BUTTONS) read live from the game exe's joystick
// probe, and its force feedback rows from the exe's wheel schema. Its own SAVE; applies at the next
// start. The probe runs only while SETUP or BUTTONS is shown and the game is not running.
public partial class MainWindow
{
    private enum WheelTab { Setup, Buttons, Ffb }

    // What a single SET waits for: an axis row, or an action's button.
    private sealed record WheelTarget(WheelAxis? Axis, string? Action);

    private static readonly WheelAxis[] Pedals = [WheelAxis.Throttle, WheelAxis.Brake, WheelAxis.Clutch, WheelAxis.Handbrake];

    private WheelSettings? _wheel;
    private string? _wheelProblem, _probeEnded, _wheelCaught, _pendingWheelType;
    private WheelTab _wheelTab = WheelTab.Setup;
    private WheelProbe? _probe;
    private bool _probeReady, _wheelExpertOpen, _wheelGameWasRunning;
    private readonly List<ProbeDevice> _wheelDevices = [];
    private readonly Dictionary<uint, ProbeState> _wheelStates = [];
    private WheelCapture? _wheelCapture;
    private WheelTarget? _wheelTarget;
    private WheelGuide? _wheelGuide;
    private WheelSettings.Edits? _wheelGuideMark;
    private Queue<WheelAction>? _wheelGates;
    // Redrawn on every probe state (bars) and every value change (texts, the unsaved mark).
    private readonly List<Action> _wheelLive = [], _wheelRefresh = [];
    private TextBlock? _wheelStatus;

    private async void LoadWheelPanel()
    {
        _probeEnded = null;
        if (_host is null || _host.Settings.ActiveVersion is null)
        {
            BuildWheelPanel("");
            return;
        }
        WheelBody.Children.Clear();
        WheelBody.Children.Add(new VectorHeading { Text = "WHEEL", FontSize = 24, Foreground = Brush("LabelBrush") });
        WheelBody.Children.Add(Help("Reading the wheel settings…"));
        try
        {
            // Edits made earlier survive switching panels; a saved panel reads the file again.
            var current = await _host.WheelAsync();
            if (current is null || _wheel?.Changed != true) _wheel = current;
            _wheelProblem = null;
        }
        catch (Exception error) when (error is IOException or UnauthorizedAccessException or InvalidDataException
                                          or System.Text.Json.JsonException or System.ComponentModel.Win32Exception)
        {
            _wheel = null;
            _wheelProblem = $"The wheel settings could not be read: {error.Message}";
        }
        if (_panel != Panel.Wheel) return;
        _wheelGameWasRunning = _host.GameRunning;
        BuildWheelPanel("");
    }

    private void LeaveWheelPanel()
    {
        CancelWheelGuide();
        EndWheelCapture();
        StopWheelProbe();
    }

    private (bool SetupEditable, bool FfbEditable, string? Message) WheelState()
    {
        var state = WheelScreenState.For(_host?.GameRunning == true, _host?.Settings.ActiveVersion is not null, _probeEnded is not null);
        return _probeEnded is { } reason && state.Message == WheelProbe.UpdateMessage ? state with { Message = reason } : state;
    }

    private void UpdateWheelActions()
    {
        var running = _host?.GameRunning == true;
        if (_panel == Panel.Wheel && running != _wheelGameWasRunning)
        {
            _wheelGameWasRunning = running;
            // The game may have changed wheel.toml from PC OPTIONS: read it again once it closes.
            if (running)
            {
                LeaveWheelPanel();
                BuildWheelPanel("");
            }
            else LoadWheelPanel();
        }
        _wheelGameWasRunning = running;
        WheelBody.IsEnabled = !_busy;
    }

    private void SyncWheelProbe()
    {
        var wanted = _panel == Panel.Wheel && _wheel is not null && _wheelTab != WheelTab.Ffb && WheelState().SetupEditable;
        if (!wanted)
        {
            StopWheelProbe();
            return;
        }
        if (_probe is not null || _host?.Settings.ActiveVersion is not { } version) return;
        _wheelDevices.Clear();
        _wheelStates.Clear();
        _probeReady = false;
        var probe = _host.NewWheelProbe(version);
        _probe = probe;
        probe.DeviceAdded += device => Dispatcher.UIThread.Post(() =>
        {
            if (_probe != probe) return;
            _wheelDevices.RemoveAll(d => d.Id == device.Id);
            _wheelDevices.Add(device);
            if (_probeReady) BuildWheelPanel(_wheelStatus?.Text ?? "");
        });
        probe.DeviceRemoved += id => Dispatcher.UIThread.Post(() =>
        {
            if (_probe != probe) return;
            _wheelDevices.RemoveAll(d => d.Id == id);
            _wheelStates.Remove(id);
            BuildWheelPanel(_wheelStatus?.Text ?? "");
        });
        probe.StateChanged += state => Dispatcher.UIThread.Post(() =>
        {
            if (_probe != probe) return;
            _wheelStates[state.Id] = state;
            if (_wheelCapture is { } capture)
            {
                capture.Feed(state, _wheelDevices);
                if (capture.Result is { } result) OnWheelCaught(result);
            }
            foreach (var update in _wheelLive) update();
        });
        probe.Ready += () => Dispatcher.UIThread.Post(() =>
        {
            if (_probe != probe) return;
            _probeReady = true;
            BuildWheelPanel(_wheelStatus?.Text ?? "");
        });
        probe.Ended += reason => Dispatcher.UIThread.Post(() =>
        {
            if (_probe != probe) return;
            _probeEnded = reason;
            CancelWheelGuide();
            EndWheelCapture();
            StopWheelProbe();
            BuildWheelPanel("");
        });
        probe.Start();
    }

    // Closing the probe waits up to 2 s for it to quit: off the UI thread.
    private void StopWheelProbe()
    {
        if (_probe is not { } probe) return;
        _probe = null;
        _probeReady = false;
        Task.Run(probe.Dispose);
    }

    private void BuildWheelPanel(string status)
    {
        WheelBody.Children.Clear();
        _wheelLive.Clear();
        _wheelRefresh.Clear();
        WheelBody.Children.Add(new VectorHeading { Text = "WHEEL", FontSize = 24, Foreground = Brush("LabelBrush") });
        _wheelStatus = Help(status);
        if (_host?.Settings.ActiveVersion is null)
        {
            WheelBody.Children.Add(Help(WheelScreenState.BuildFirst));
            return;
        }
        if (_wheel is null)
        {
            WheelBody.Children.Add(Help(_wheelProblem ?? "WHEEL needs game version 0.4.0 or later. Update the game to use a racing wheel."));
            return;
        }
        if (_wheel.FileIsMalformed)
        {
            WheelBody.Children.Add(Help("wheel.toml is not a list of key = value lines, so the wheel settings cannot be shown. "
                                        + "Edit or delete the file, then open this panel again."));
            return;
        }
        var wheel = _wheel;
        var state = WheelState();

        // Without a throttle the wheel is turned on only by finishing the set-up, so a cancelled
        // set-up never leaves a car that does not accelerate.
        var needsSetUp = !wheel.Enabled && wheel.Axis(WheelAxis.Throttle) is null;
        var enabled = new CheckBox
        {
            IsChecked = wheel.Enabled || _wheelGuide is not null, Content = new TextBlock { Text = "Use a racing wheel" },
            IsEnabled = state.FfbEditable && _wheelGuide is null && (!needsSetUp || state.SetupEditable),
        };
        if (needsSetUp && !state.SetupEditable) ToolTip.SetTip(enabled, "Close the game to set up the wheel first.");
        AutomationProperties.SetName(enabled, "Use a racing wheel");
        enabled.IsCheckedChanged += (_, _) =>
        {
            if (enabled.IsChecked == true && needsSetUp)
            {
                _wheelTab = WheelTab.Setup;
                StartWheelGuide();
                return;
            }
            wheel.Enabled = enabled.IsChecked == true;
            RefreshWheel();
        };
        WheelBody.Children.Add(enabled);

        var unsaved = new TextBlock { Text = "*", Foreground = Brush("AccentBrush"), FontWeight = FontWeight.Bold };
        _wheelRefresh.Add(() => unsaved.IsVisible = wheel.Changed);
        var save = new Button
        {
            Content = new StackPanel { Orientation = Orientation.Horizontal, Spacing = 6, Children = { new TextBlock { Text = "SAVE" }, unsaved } },
            MinWidth = 120, HorizontalContentAlignment = HorizontalAlignment.Center, Margin = new Thickness(0, 0, 10, 0), Classes = { "quiet" },
        };
        AutomationProperties.SetName(save, "SAVE");
        save.Click += (_, _) => SaveWheel();
        var resetAll = new Button { Content = "RESET ALL", Classes = { "quiet" }, IsEnabled = _wheelGuide is null };
        resetAll.Click += (_, _) => ResetWheelTab();
        resetAll.IsEnabled = _wheelGuide is null && (_wheelTab == WheelTab.Ffb ? state.FfbEditable : state.SetupEditable);
        WheelBody.Children.Add(new WrapPanel { Children = { save, resetAll } });
        WheelBody.Children.Add(_wheelStatus);

        if (_wheelGuide is not null)
        {
            AddWheelGuideView();
            RefreshWheel();
            return;
        }

        var tabs = new WrapPanel();
        foreach (var (tab, label) in new[] { (WheelTab.Setup, "SETUP"), (WheelTab.Buttons, "BUTTONS"), (WheelTab.Ffb, "FORCE FEEDBACK") })
        {
            var button = new Button { Content = label, Classes = { "quiet", "nav" } };
            button.Classes.Set("selected", tab == _wheelTab);
            button.Click += (_, _) =>
            {
                EndWheelCapture();
                _wheelTab = tab;
                BuildWheelPanel("");
            };
            tabs.Children.Add(button);
        }
        WheelBody.Children.Add(tabs);

        if (_wheelTab == WheelTab.Ffb) AddWheelFfbRows(wheel, state.FfbEditable);
        else
        {
            if (state.Message is { } message) WheelBody.Children.Add(new TextBlock { Text = message, Foreground = Brush("AccentBrush"), TextWrapping = TextWrapping.Wrap });
            else AddWheelDeviceNote();
            var rows = new StackPanel { Spacing = 10, IsEnabled = state.SetupEditable };
            if (_wheelTab == WheelTab.Setup) AddWheelSetupRows(wheel, rows);
            else AddWheelButtonRows(wheel, rows);
            WheelBody.Children.Add(rows);
        }
        RefreshWheel();
        SyncWheelProbe();
    }

    private void AddWheelDeviceNote()
    {
        if (_probe is null || !_probeReady) WheelBody.Children.Add(Help("Looking for your wheel…"));
        else if (_wheelDevices.Count == 0) WheelBody.Children.Add(Help("No wheel found. Plug it in; it shows up here."));
    }

    private void RefreshWheel()
    {
        foreach (var refresh in _wheelRefresh) refresh();
        foreach (var update in _wheelLive) update();
    }

    private void AddWheelSetupRows(WheelSettings wheel, StackPanel rows)
    {
        rows.Children.Add(Help("Close your wheel maker's own control panel while you set up and play."));
        var steeringDevice = SteeringDevice(wheel);
        var match = wheel.Expert("wheel_device");
        var baseName = steeringDevice?.Name ?? (match.Length > 0 ? DeviceText(match + "|") : "none found");
        rows.Children.Add(WheelLine("Wheel base", new TextBlock
        {
            Text = baseName + (match.Length == 0 && steeringDevice is not null ? "   (found automatically)" : ""),
            Foreground = Brush("ValueBrush"), VerticalAlignment = VerticalAlignment.Center,
        }));

        var invert = new CheckBox { IsChecked = wheel.SteeringInvert, Content = new TextBlock { Text = "invert" } };
        AutomationProperties.SetName(invert, "Invert steering");
        invert.IsCheckedChanged += (_, _) =>
        {
            wheel.SteeringInvert = invert.IsChecked == true;
            RefreshWheel();
        };
        rows.Children.Add(WheelLine("Steering", AxisView(wheel, WheelAxis.Steering), SetButton(new WheelTarget(WheelAxis.Steering, null), "Steering"), invert));

        foreach (var axis in Pedals) rows.Children.Add(PedalRow(wheel, axis));

        var rotation = new NumericUpDown
        {
            Minimum = WheelSettings.MinSteeringLockDegrees, Maximum = 3600, Increment = 10, Value = wheel.Rotation, FormatString = "0",
            Width = 140, HorizontalAlignment = HorizontalAlignment.Left,
        };
        AutomationProperties.SetName(rotation, "Wheel rotation");
        rotation.ValueChanged += (_, e) =>
        {
            if (e.NewValue is { } degrees) wheel.Rotation = (int)degrees;
            RefreshWheel();
        };
        rows.Children.Add(WheelLine("Wheel rotation", new StackPanel
        {
            Spacing = 2,
            Children = { rotation, Help("Degrees, as set in your wheel's own software.") },
        }));

        var again = new Button { Content = "RUN SETUP AGAIN", Classes = { "quiet" }, HorizontalAlignment = HorizontalAlignment.Left };
        again.Click += (_, _) => StartWheelGuide();
        rows.Children.Add(again);
    }

    private Control PedalRow(WheelSettings wheel, WheelAxis axis)
    {
        var label = axis.ToString();
        var name = axis switch { WheelAxis.Throttle => "throttle", WheelAxis.Brake => "brake", WheelAxis.Clutch => "clutch", _ => "handbrake" };
        var right = new List<Control> { SetButton(new WheelTarget(axis, null), label) };
        if (axis is WheelAxis.Clutch or WheelAxis.Handbrake && wheel.Axis(axis) is not null)
        {
            var clear = ClearButton($"Clear {label}");
            clear.Click += (_, _) =>
            {
                EndWheelCapture();
                wheel.SetAxis(axis, null);
                BuildWheelPanel("");
            };
            right.Add(clear);
        }
        var column = new StackPanel { Spacing = 2 };
        column.Children.Add(WheelLine(label, AxisView(wheel, axis), right.ToArray()));
        if (wheel.Axis(axis) is { } binding && WheelBinding.Parse(binding) is { Kind: CaptureKind.Axis } parsed)
        {
            var device = ResolveDevice(parsed.Device, wheel);
            var rest = device is not null && parsed.Index < device.Rest.Count ? device.Rest[parsed.Index] : (short?)null;
            var invert = new CheckBox
            {
                IsChecked = parsed.Sign is not null, Content = new TextBlock { Text = "invert" },
                IsEnabled = parsed.Sign is not null || rest is not null,
            };
            ToolTip.SetTip(invert, "Tick it if the bar goes down when you press.");
            AutomationProperties.SetName(invert, $"Invert {label}");
            invert.IsCheckedChanged += (_, _) =>
            {
                // The SDK reads a pedal resting above the middle as running down; a sign overrides that.
                char? sign = invert.IsChecked == true ? (rest > 0 ? '+' : '-') : null;
                wheel.SetAxis(axis, (parsed.Device.Length > 0 ? parsed.Device + "|" : "") + sign + "axis" + parsed.Index);
                BuildWheelPanel("");
            };
            var zones = new StackPanel
            {
                Orientation = Orientation.Horizontal, Spacing = 8,
                Children =
                {
                    invert,
                    new TextBlock { Text = "dead zone", VerticalAlignment = VerticalAlignment.Center, Margin = new Thickness(12, 0, 0, 0) },
                    DeadZoneBox(wheel, $"wheel_{name}_deadzone_bottom", $"{label} dead zone at rest"),
                    new TextBlock { Text = "% / top", VerticalAlignment = VerticalAlignment.Center },
                    DeadZoneBox(wheel, $"wheel_{name}_deadzone_top", $"{label} dead zone at full travel"),
                    new TextBlock { Text = "%", VerticalAlignment = VerticalAlignment.Center },
                },
            };
            column.Children.Add(WheelLine("", zones));
        }
        return column;
    }

    // -1 at rest means the shared wheel_pedal_deadzone: shown as that value.
    private Control DeadZoneBox(WheelSettings wheel, string key, string label)
    {
        var stored = int.TryParse(wheel.Expert(key), NumberStyles.Integer, CultureInfo.InvariantCulture, out var value) ? value : 0;
        if (stored < 0) stored = int.TryParse(wheel.Expert("wheel_pedal_deadzone"), NumberStyles.Integer, CultureInfo.InvariantCulture, out var shared) ? shared : 0;
        var box = new NumericUpDown { Minimum = 0, Maximum = 50, Increment = 1, Value = stored, FormatString = "0", Width = 110 };
        AutomationProperties.SetName(box, label);
        box.ValueChanged += (_, e) =>
        {
            if (e.NewValue is { } percent) wheel.SetExpert(key, ((int)percent).ToString(CultureInfo.InvariantCulture));
            RefreshWheel();
        };
        return box;
    }

    private void AddWheelButtonRows(WheelSettings wheel, StackPanel rows)
    {
        rows.Children.Add(Help("The small button names (A, RB, …) are the game's default wheel layout. "
                               + "If you chose another wheel layout in the game's options, an action may be on a different button."));
        foreach (var action in WheelActions.All.Where(a => !a.IsGate))
        {
            var labels = new StackPanel { VerticalAlignment = VerticalAlignment.Center, Children = { new TextBlock { Text = action.Label } } };
            labels.Children.Add(new TextBlock
            {
                Text = action.Pad + (action.MenuMeaning is { } menu ? $"   menu: {menu}" : ""), Classes = { "help" },
            });
            var right = new List<Control> { SetButton(new WheelTarget(null, action.Id), action.Label) };
            if (wheel.Button(action.Id) is not null)
            {
                var clear = ClearButton($"Clear {action.Label}");
                clear.Click += (_, _) =>
                {
                    EndWheelCapture();
                    wheel.SetButton(action.Id, null);
                    BuildWheelPanel("");
                };
                right.Add(clear);
            }
            rows.Children.Add(WheelLine(labels, ButtonView(wheel, action.Id), right.ToArray()));
        }
        rows.Children.Add(WheelLine("D-pad", new TextBlock
        {
            Text = "hat 0 (found automatically)", Foreground = Brush("ValueBrush"), VerticalAlignment = VerticalAlignment.Center,
        }));

        var gates = WheelActions.All.Where(a => a.IsGate).ToList();
        var setGates = gates.Count(g => wheel.Button(g.Id) is not null);
        var setAll = new Button { Content = "SET ALL", Classes = { "quiet" }, Margin = new Thickness(0, 0, 4, 4) };
        AutomationProperties.SetName(setAll, "Set all H-shifter gates");
        setAll.Click += (_, _) =>
        {
            _wheelGates = new Queue<WheelAction>(gates);
            NextWheelGate();
        };
        var gateRight = new List<Control> { setAll };
        if (setGates > 0)
        {
            var clear = ClearButton("Clear the H-shifter gates");
            clear.Click += (_, _) =>
            {
                EndWheelCapture();
                foreach (var gate in gates) wheel.SetButton(gate.Id, null);
                BuildWheelPanel("");
            };
            gateRight.Add(clear);
        }
        if (_wheelGates is not null)
        {
            var skip = new Button { Content = "SKIP", Classes = { "quiet" }, Margin = new Thickness(0, 0, 4, 4) };
            skip.Click += (_, _) =>
            {
                if (_wheelTarget?.Action is { } gate) wheel.SetButton(gate, null);
                NextWheelGate();
            };
            gateRight.Add(skip);
        }
        rows.Children.Add(WheelLine(new StackPanel
        {
            VerticalAlignment = VerticalAlignment.Center,
            Children = { new TextBlock { Text = "H-shifter gates" }, new TextBlock { Text = "1–7, R (if you have one)", Classes = { "help" } } },
        }, new TextBlock
        {
            Text = setGates == 0 ? "— not set" : $"{setGates} of {gates.Count} set", Foreground = Brush("ValueBrush"),
            VerticalAlignment = VerticalAlignment.Center,
        }, gateRight.ToArray()));
    }

    private void NextWheelGate()
    {
        if (_wheelGates is not { Count: > 0 } gates)
        {
            _wheelGates = null;
            EndWheelCapture();
            BuildWheelPanel("H-shifter gates set. SAVE keeps them.");
            return;
        }
        var gate = gates.Dequeue();
        StartWheelCapture(new WheelTarget(null, gate.Id),
            gate.Pad == "REVERSE" ? "Put the shifter in reverse, or press SKIP." : $"Put the shifter in gear {gate.Pad[4..]}, or press SKIP.");
    }

    private void AddWheelFfbRows(WheelSettings wheel, bool editable)
    {
        WheelBody.Children.Add(Help("These apply at the next start. In a race, the same rows are in the game's PC OPTIONS → WHEEL."));
        var rows = new StackPanel { Spacing = 10, IsEnabled = editable };

        var types = new WrapPanel { VerticalAlignment = VerticalAlignment.Center };
        foreach (var type in wheel.Schema.Types)
        {
            var button = new Button { Content = type.Label, Classes = { "quiet", "nav" } };
            button.Click += (_, _) =>
            {
                if (wheel.WheelType == "custom" && _pendingWheelType != type.Id)
                {
                    _pendingWheelType = type.Id;
                    if (_wheelStatus is not null) _wheelStatus.Text = $"This replaces your custom values: click {type.Label} again.";
                    return;
                }
                _pendingWheelType = null;
                wheel.SetWheelType(type.Id);
                BuildWheelPanel("");
            };
            _wheelRefresh.Add(() => button.Classes.Set("selected", wheel.WheelType == type.Id));
            types.Children.Add(button);
        }
        var custom = new TextBlock { Text = "CUSTOM", Foreground = Brush("AccentBrush"), VerticalAlignment = VerticalAlignment.Center, Margin = new Thickness(8, 0) };
        _wheelRefresh.Add(() => custom.IsVisible = wheel.WheelType == "custom");
        types.Children.Add(custom);
        rows.Children.Add(WheelLine(new StackPanel
        {
            VerticalAlignment = VerticalAlignment.Center,
            Children = { new TextBlock { Text = "Wheel type" }, new TextBlock { Text = "Sets every Advanced row.", Classes = { "help" } } },
        }, types));

        foreach (var row in wheel.Schema.Rows.Where(r => r.Basic)) rows.Children.Add(FfbRow(wheel, row));
        rows.Children.Add(new TextBlock { Text = "ADVANCED", Classes = { "header" }, Margin = new Thickness(0, 12, 0, 0) });
        rows.Children.Add(Help("Changing one of these makes the wheel type Custom."));
        foreach (var row in wheel.Schema.Rows.Where(r => !r.Basic)) rows.Children.Add(FfbRow(wheel, row));

        var expert = new Button
        {
            Content = (_wheelExpertOpen ? "HIDE" : "SHOW") + $" EXPERT SETTINGS ({wheel.Schema.Expert.Count})", Classes = { "link" }, FontSize = 16,
            Padding = new Thickness(0, 10), HorizontalAlignment = HorizontalAlignment.Left,
        };
        expert.Click += (_, _) =>
        {
            _wheelExpertOpen = !_wheelExpertOpen;
            BuildWheelPanel(_wheelStatus?.Text ?? "");
        };
        rows.Children.Add(expert);
        if (_wheelExpertOpen)
        {
            rows.Children.Add(Help("The game's own wheel options, as written in wheel.toml. A wrong value can stop the wheel working; "
                                   + "clear a box to go back to the default."));
            foreach (var setting in wheel.Schema.Expert)
            {
                var box = new TextBox { Text = wheel.Expert(setting.Key), Width = 360, HorizontalAlignment = HorizontalAlignment.Left };
                AutomationProperties.SetName(box, setting.Key);
                box.TextChanged += (_, _) =>
                {
                    wheel.SetExpert(setting.Key, string.IsNullOrEmpty(box.Text) ? setting.Default : box.Text);
                    RefreshWheel();
                };
                rows.Children.Add(new StackPanel
                {
                    Spacing = 2,
                    Children =
                    {
                        new TextBlock { Text = setting.Key, FontFamily = new FontFamily("monospace"), FontSize = 13 },
                        Help(setting.Description), box,
                    },
                });
            }
        }
        WheelBody.Children.Add(rows);
    }

    private Control FfbRow(WheelSettings wheel, WheelRow row)
    {
        var steeringLock = row.Key == WheelSchema.SteeringLockKey;
        var slider = new Slider
        {
            Minimum = steeringLock ? wheel.MinSteeringLock : row.Min, Maximum = steeringLock ? wheel.Rotation : row.Max,
            Value = wheel.Value(row.Key), SmallChange = row.Step, LargeChange = row.Step * 2, IsSnapToTickEnabled = true,
            TickFrequency = row.Step, Width = 320,
        };
        var value = new TextBlock { Width = 80, VerticalAlignment = VerticalAlignment.Center, Foreground = Brush("ValueBrush") };
        _wheelRefresh.Add(() => value.Text = wheel.Value(row.Key).ToString(CultureInfo.InvariantCulture) + (steeringLock ? "°" : " %"));
        slider.ValueChanged += (_, e) =>
        {
            wheel.SetValue(row.Key, (int)Math.Round(e.NewValue));
            RefreshWheel();
        };
        AutomationProperties.SetName(slider, row.Label);
        var line = new StackPanel { Orientation = Orientation.Horizontal, Spacing = 10, Children = { slider, value } };
        return WheelLine(new StackPanel
        {
            VerticalAlignment = VerticalAlignment.Center,
            Children = { new TextBlock { Text = Capitalised(row.Label) }, new TextBlock { Text = row.Description, Classes = { "help" }, TextWrapping = TextWrapping.Wrap } },
        }, line);
    }

    private static string Capitalised(string label) => label[..1] + label[1..].ToLowerInvariant();

    private void AddWheelGuideView()
    {
        var guide = _wheelGuide!;
        var view = new StackPanel { Spacing = 10 };
        view.Children.Add(new TextBlock { Text = "WHEEL SET-UP", Classes = { "header" } });
        view.Children.Add(new TextBlock { Text = guide.Prompt, FontSize = 20, TextWrapping = TextWrapping.Wrap, Foreground = Brush("LabelBrush") });
        if (_wheelCaught is { } caught) view.Children.Add(Help($"Got it: {caught}."));
        if (_probe is null || !_probeReady) view.Children.Add(Help("Looking for your wheel…"));
        else if (_wheelDevices.Count == 0) view.Children.Add(Help("No wheel found. Plug it in; it shows up here."));
        var buttons = new WrapPanel();
        if (guide.CanSkip)
        {
            var skip = new Button { Content = "SKIP", Classes = { "quiet" }, Margin = new Thickness(0, 0, 10, 0) };
            skip.Click += (_, _) =>
            {
                guide.Skip();
                _wheelCaught = null;
                NextWheelGuideStep();
            };
            buttons.Children.Add(skip);
        }
        var cancel = new Button { Content = "CANCEL", Classes = { "quiet" } };
        cancel.Click += (_, _) =>
        {
            CancelWheelGuide();
            BuildWheelPanel("Set-up cancelled; nothing changed.");
        };
        buttons.Children.Add(cancel);
        view.Children.Add(buttons);
        WheelBody.Children.Add(new Border
        {
            BorderBrush = Brush("AccentBrush"), BorderThickness = new Thickness(1), Padding = new Thickness(18, 14), Child = view,
        });
        SyncWheelProbe();
    }

    private void StartWheelGuide()
    {
        if (_wheel is null) return;
        EndWheelCapture();
        _wheelGates = null;
        _wheelGuideMark = _wheel.Mark();
        _wheelGuide = new WheelGuide(_wheel);
        _wheelCaught = null;
        _wheelTab = WheelTab.Setup;
        NextWheelGuideStep();
    }

    private void NextWheelGuideStep()
    {
        if (_wheelGuide is not { } guide) return;
        if (guide.Done)
        {
            _wheelGuide = null;
            _wheelGuideMark = null;
            _wheelCapture = null;
            _wheel!.Enabled = true;
            BuildWheelPanel("Set-up done. Press SAVE to keep it.");
            return;
        }
        _wheelCapture = PrimedCapture(guide.NewCapture());
        BuildWheelPanel("");
    }

    private void CancelWheelGuide()
    {
        if (_wheelGuide is null) return;
        if (_wheelGuideMark is { } mark) _wheel?.Revert(mark);
        _wheelGuide = null;
        _wheelGuideMark = null;
        _wheelCapture = null;
        _wheelCaught = null;
    }

    private void StartWheelCapture(WheelTarget target, string prompt)
    {
        if (_wheel is null) return;
        _wheelTarget = target;
        var taken = TakenAxes(_wheel, target.Axis);
        _wheelCapture = PrimedCapture(target.Axis switch
        {
            WheelAxis.Steering => WheelCapture.ForAxis(steering: true, taken),
            WheelAxis.Handbrake => WheelCapture.ForAxisOrButton(taken),
            null => WheelCapture.ForButton(),
            _ => WheelCapture.ForAxis(steering: false, taken),
        });
        BuildWheelPanel(prompt);
    }

    private void EndWheelCapture()
    {
        if (_wheelGuide is null) _wheelCapture = null;
        _wheelTarget = null;
        _wheelGates = null;
    }

    // The capture's starting point is every device's state now.
    private WheelCapture PrimedCapture(WheelCapture capture)
    {
        foreach (var state in _wheelStates.Values) capture.Feed(state, _wheelDevices);
        return capture;
    }

    private void OnWheelCaught(CaptureResult result)
    {
        if (_wheel is not { } wheel) return;
        _wheelCaught = Describe(result.Binding);
        if (_wheelGuide is { } guide)
        {
            guide.Accept(result);
            NextWheelGuideStep();
            return;
        }
        var target = _wheelTarget;
        _wheelCapture = null;
        _wheelTarget = null;
        switch (target)
        {
            case { Axis: WheelAxis.Steering }:
                wheel.SetAxis(WheelAxis.Steering, result.Binding);
                wheel.SteeringInvert = result.Positive;
                break;
            case { Axis: WheelAxis.Handbrake } when result.Kind != CaptureKind.Axis:
                wheel.SetAxis(WheelAxis.Handbrake, null);
                wheel.SetButton("handbrake", result.Binding);
                break;
            case { Axis: { } axis }:
                wheel.SetAxis(axis, result.Binding);
                break;
            case { Action: { } action }:
                wheel.SetButton(action, result.Binding);
                if (_wheelGates is not null)
                {
                    NextWheelGate();
                    return;
                }
                break;
        }
        BuildWheelPanel($"Set to {_wheelCaught}.");
    }

    private void SaveWheel()
    {
        if (_wheel is null) return;
        string status;
        try
        {
            _wheel.Save();
            status = "Saved. Applies at the next start.";
            AppendLog("Wheel settings saved.");
        }
        catch (Exception error) when (error is IOException or UnauthorizedAccessException)
        {
            status = $"Not saved: {error.Message}";
        }
        BuildWheelPanel(status);
    }

    private void ResetWheelTab()
    {
        if (_wheel is not { } wheel) return;
        EndWheelCapture();
        switch (_wheelTab)
        {
            case WheelTab.Setup:
                wheel.SetAxis(WheelAxis.Steering, null);
                wheel.SteeringInvert = false;
                foreach (var axis in Pedals)
                {
                    wheel.SetAxis(axis, null);
                    var name = WheelSettings.AxisKey(axis);
                    wheel.SetExpert(name + "_deadzone_bottom", DefaultOf(wheel, name + "_deadzone_bottom"));
                    wheel.SetExpert(name + "_deadzone_top", DefaultOf(wheel, name + "_deadzone_top"));
                }
                wheel.Rotation = WheelSettings.DefaultRotation;
                break;
            case WheelTab.Buttons:
                wheel.SetExpert("wheel_buttons", DefaultOf(wheel, "wheel_buttons"));
                break;
            case WheelTab.Ffb:
                wheel.SetWheelType(wheel.Schema.Types[0].Id);
                foreach (var row in wheel.Schema.Rows.Where(r => r.Basic)) wheel.SetValue(row.Key, row.Fallback);
                wheel.SteeringLock = wheel.Rotation;
                break;
        }
        BuildWheelPanel("Defaults shown for this tab; SAVE writes them.");
    }

    private static string DefaultOf(WheelSettings wheel, string key) => wheel.Schema.Expert.Single(e => e.Key == key).Default;

    private Button SetButton(WheelTarget target, string label)
    {
        var capturing = _wheelTarget == target && _wheelCapture is not null;
        var button = new Button { Content = capturing ? "WAITING…" : "SET", Classes = { "quiet" }, Margin = new Thickness(0, 0, 4, 4) };
        if (capturing) button.Foreground = Brush("AccentBrush");
        AutomationProperties.SetName(button, capturing ? $"{label}: waiting" : $"Set {label}");
        button.Click += (_, _) =>
        {
            if (capturing)
            {
                EndWheelCapture();
                BuildWheelPanel("");
                return;
            }
            _wheelGates = null;
            StartWheelCapture(target, PromptFor(target));
        };
        return button;
    }

    private static string PromptFor(WheelTarget target) => target.Axis switch
    {
        WheelAxis.Steering => "Turn the wheel all the way to the left. Press WAITING… to stop.",
        WheelAxis.Throttle => "Press the throttle pedal all the way down.",
        WheelAxis.Brake => "Press the brake pedal all the way down.",
        WheelAxis.Clutch => "Press the clutch pedal all the way down.",
        WheelAxis.Handbrake => "Pull the handbrake.",
        _ => $"Press the button you want for {WheelActions.Of(target.Action!).Label.ToLowerInvariant()}.",
    };

    private Button ClearButton(string name)
    {
        var button = new Button
        {
            Content = "✕", Classes = { "quiet" }, FontSize = 17, MinWidth = 42, Padding = new Thickness(0, 10),
            Margin = new Thickness(0, 0, 4, 4), HorizontalContentAlignment = HorizontalAlignment.Center,
        };
        AutomationProperties.SetName(button, name);
        return button;
    }

    private Control WheelLine(string label, Control middle, params Control[] right) =>
        WheelLine(new TextBlock { Text = label, VerticalAlignment = VerticalAlignment.Center }, middle, right);

    private static Control WheelLine(Control label, Control middle, params Control[] right)
    {
        var line = new Grid { ColumnDefinitions = new ColumnDefinitions("200,*,170") };
        line.Children.Add(label);
        Grid.SetColumn(middle, 1);
        line.Children.Add(middle);
        var buttons = new StackPanel { Orientation = Orientation.Horizontal, Spacing = 4, VerticalAlignment = VerticalAlignment.Center };
        foreach (var control in right) buttons.Children.Add(control);
        Grid.SetColumn(buttons, 2);
        line.Children.Add(buttons);
        return line;
    }

    // A bar that follows the bound input live, and what it is bound to.
    private Control AxisView(WheelSettings wheel, WheelAxis axis)
    {
        var binding = wheel.Axis(axis);
        if (binding is null && axis == WheelAxis.Handbrake && wheel.Button("handbrake") is { } button) binding = button;
        var bar = new ProgressBar { Minimum = 0, Maximum = 100, Height = 8, Width = 200, CornerRadius = new CornerRadius(0), Foreground = Brush("AccentBrush") };
        var text = new TextBlock
        {
            Text = binding is null ? "— not set" : Describe(binding), Foreground = Brush("ValueBrush"), VerticalAlignment = VerticalAlignment.Center,
            TextTrimming = TextTrimming.CharacterEllipsis,
        };
        if (binding is not null)
            _wheelLive.Add(() => bar.Value = (axis == WheelAxis.Steering ? SteeringPercent(wheel) : InputPercent(wheel, binding)) ?? 0);
        return new StackPanel { Orientation = Orientation.Horizontal, Spacing = 10, Children = { bar, text } };
    }

    private Control ButtonView(WheelSettings wheel, string actionId)
    {
        var binding = wheel.Button(actionId);
        var text = new TextBlock
        {
            Text = binding is null ? "— not set" : Describe(binding), Foreground = Brush("ValueBrush"), VerticalAlignment = VerticalAlignment.Center,
        };
        if (binding is not null)
            _wheelLive.Add(() => text.Foreground = InputPercent(wheel, binding) > 50 ? Brush("AccentBrush") : Brush("ValueBrush"));
        return text;
    }

    // The steering device: wheel_device, or the first device the probe calls a wheel, as the SDK picks it.
    private ProbeDevice? SteeringDevice(WheelSettings wheel)
    {
        var match = wheel.Expert("wheel_device");
        return match.Length > 0 ? WheelDevices.Find(match, _wheelDevices) : WheelDevices.SteeringCandidate(_wheelDevices);
    }

    // An empty device is the wheel base.
    private ProbeDevice? ResolveDevice(string device, WheelSettings wheel) =>
        device.Length > 0 ? WheelDevices.Find(device, _wheelDevices) : SteeringDevice(wheel);

    private int? SteeringPercent(WheelSettings wheel)
    {
        if (SteeringDevice(wheel) is not { } device || !_wheelStates.TryGetValue(device.Id, out var state)) return null;
        var index = int.TryParse(wheel.Expert("wheel_steering_axis"), NumberStyles.Integer, CultureInfo.InvariantCulture, out var axis) ? axis : 0;
        if (index >= state.Axes.Count) return null;
        var percent = (int)Math.Round((state.Axes[index] + 32768) * 100.0 / 65535);
        return wheel.SteeringInvert ? 100 - percent : percent;
    }

    private int? InputPercent(WheelSettings wheel, string binding)
    {
        if (WheelBinding.Parse(binding) is not { } parsed || ResolveDevice(parsed.Device, wheel) is not { } device
            || !_wheelStates.TryGetValue(device.Id, out var state)) return null;
        return parsed.Kind switch
        {
            CaptureKind.Axis when parsed.Index < state.Axes.Count && parsed.Index < device.Rest.Count =>
                WheelBinding.PedalPercent(state.Axes[parsed.Index], device.Rest[parsed.Index], parsed.Sign),
            CaptureKind.Button when parsed.Index < state.Buttons.Count => state.Buttons[parsed.Index] ? 100 : 0,
            CaptureKind.Hat when parsed.Index < state.Hats.Count =>
                (state.Hats[parsed.Index] & HatBit(parsed.HatDirection)) != 0 ? 100 : 0,
            _ => null,
        };
    }

    private static int HatBit(string? direction) => direction switch { "up" => 1, "right" => 2, "down" => 4, "left" => 8, _ => 0 };

    private string Describe(string binding)
    {
        if (WheelBinding.Parse(binding) is not { } parsed) return binding;
        var input = parsed.Kind switch
        {
            CaptureKind.Axis => $"axis {parsed.Index}",
            CaptureKind.Button => $"button {parsed.Index}",
            _ => $"hat {parsed.Index} {parsed.HatDirection}",
        };
        return parsed.Device.Length > 0 ? $"{DeviceText(parsed.Device + "|")} {input}" : input;
    }

    // A device's name while connected, else the match text wheel.toml holds.
    private string DeviceText(string binding)
    {
        var device = binding.LastIndexOf('|') is var bar and >= 0 ? binding[..bar] : binding;
        return WheelDevices.Find(device, _wheelDevices)?.Name ?? device;
    }

    private static List<(uint, int)> TakenAxes(WheelSettings wheel, WheelAxis? except, IReadOnlyList<ProbeDevice> devices, Func<string, ProbeDevice?> resolve)
    {
        var taken = new List<(uint, int)>();
        foreach (var axis in Pedals.Prepend(WheelAxis.Steering))
        {
            if (axis == except || wheel.Axis(axis) is not { } binding || WheelBinding.Parse(binding) is not { Kind: CaptureKind.Axis } parsed) continue;
            if (resolve(parsed.Device) is { } device && devices.Contains(device)) taken.Add((device.Id, parsed.Index));
        }
        return taken;
    }

    private List<(uint, int)> TakenAxes(WheelSettings wheel, WheelAxis? except) =>
        TakenAxes(wheel, except, _wheelDevices, device => ResolveDevice(device, wheel));
}
