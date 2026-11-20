// Portions copyright (c) 2026, Pinyon Shift contributors (BSD 3-Clause: see THIRD_PARTY_NOTICES.md).
// Copyright (c) 2026, i2kiselev (BSD 3-Clause: see LICENSE in the repository root).
// Adapted from Pinyon Shift's launcher (launcher/PinyonShift.Launcher/Wheel/Bindings/Capture.cs, BSD-3-Clause; see THIRD_PARTY_NOTICES.md).
namespace Fm4StaticRecomp.Launcher.Core.Wheel;

public enum CaptureKind { Axis, Button, Hat }

// Device: the match that names the device in wheel.toml. Index: the axis, button or hat; HatDirection
// for a hat. Rest: the axis value when the capture began. Positive: the axis moved up from it.
public sealed record CaptureResult(string Device, uint DeviceId, CaptureKind Kind, int Index, short Rest, bool Positive,
    string? HatDirection = null)
{
    // As the SDK reads it: "device|axisN", "device|N" or "device|hN.dir". No sign on an axis: the SDK
    // finds a pedal's direction from where it rests.
    public string Binding => Device + "|" + Kind switch
    {
        CaptureKind.Axis => $"axis{Index}",
        CaptureKind.Button => $"{Index}",
        _ => $"h{Index}.{HatDirection}",
    };
}

// Listens for the input a step asks for: the axis that moves furthest from where it was when the
// capture began (steering, pedals), or the first button or hat direction pressed after it began.
// The first state fed for a device is its starting point, so feed every device's current state first.
public sealed class WheelCapture
{
    private const int SteeringThreshold = 10000, PedalThreshold = 16000;
    private static readonly (byte Bit, string Name)[] HatDirections = [(1, "up"), (2, "right"), (4, "down"), (8, "left")];

    private readonly int _axisThreshold;
    private readonly bool _buttons;
    private readonly HashSet<(uint Device, int Axis)> _taken;
    private readonly Dictionary<uint, short[]> _baseAxes = [];
    private readonly Dictionary<uint, bool[]> _baseButtons = [];
    private readonly Dictionary<uint, byte[]> _baseHats = [];

    private WheelCapture(int axisThreshold, bool buttons, IEnumerable<(uint, int)> taken)
    {
        _axisThreshold = axisThreshold;
        _buttons = buttons;
        _taken = taken.ToHashSet();
    }

    public static WheelCapture ForAxis(bool steering, IEnumerable<(uint, int)> taken) =>
        new(steering ? SteeringThreshold : PedalThreshold, buttons: false, taken);

    public static WheelCapture ForButton() => new(axisThreshold: 0, buttons: true, []);

    // The handbrake: a lever axis or a button.
    public static WheelCapture ForAxisOrButton(IEnumerable<(uint, int)> taken) => new(PedalThreshold, buttons: true, taken);

    public CaptureResult? Result { get; private set; }

    public void Feed(ProbeState state, IReadOnlyList<ProbeDevice> devices)
    {
        if (Result is not null || devices.FirstOrDefault(d => d.Id == state.Id) is not { } device) return;
        if (!_baseAxes.ContainsKey(state.Id))
        {
            _baseAxes[state.Id] = state.Axes.ToArray();
            _baseButtons[state.Id] = state.Buttons.ToArray();
            _baseHats[state.Id] = state.Hats.ToArray();
            return;
        }
        if (_buttons && TrackButtons(state, device, devices)) return;
        if (_axisThreshold > 0) TrackAxes(state, device, devices);
    }

    private void TrackAxes(ProbeState state, ProbeDevice device, IReadOnlyList<ProbeDevice> devices)
    {
        var baseAxes = _baseAxes[state.Id];
        int best = 0, bestAxis = -1;
        for (var i = 0; i < state.Axes.Count && i < baseAxes.Length; i++)
        {
            if (_taken.Contains((state.Id, i))) continue;
            var delta = state.Axes[i] - baseAxes[i];
            if (Math.Abs(delta) > Math.Abs(best)) (best, bestAxis) = (delta, i);
        }
        if (bestAxis < 0 || Math.Abs(best) <= _axisThreshold) return;
        Result = new CaptureResult(WheelDevices.Match(device, devices), state.Id, CaptureKind.Axis, bestAxis, baseAxes[bestAxis], best > 0);
    }

    // A button or hat held when the capture began (an H-shifter left in gear) counts once it is let
    // go and pressed again.
    private bool TrackButtons(ProbeState state, ProbeDevice device, IReadOnlyList<ProbeDevice> devices)
    {
        var baseButtons = _baseButtons[state.Id];
        var baseHats = _baseHats[state.Id];
        for (var i = 0; i < state.Buttons.Count && i < baseButtons.Length; i++)
            if (!state.Buttons[i]) baseButtons[i] = false;
        for (var i = 0; i < state.Hats.Count && i < baseHats.Length; i++)
            baseHats[i] &= state.Hats[i];
        var match = WheelDevices.Match(device, devices);
        for (var i = 0; i < state.Buttons.Count && i < baseButtons.Length; i++)
        {
            if (!state.Buttons[i] || baseButtons[i]) continue;
            Result = new CaptureResult(match, state.Id, CaptureKind.Button, i, Rest: 0, Positive: true);
            return true;
        }
        for (var i = 0; i < state.Hats.Count && i < baseHats.Length; i++)
        {
            var pressed = (byte)(state.Hats[i] & ~baseHats[i]);
            if (HatDirections.FirstOrDefault(h => (pressed & h.Bit) != 0) is not { Name: { } name }) continue;
            Result = new CaptureResult(match, state.Id, CaptureKind.Hat, i, Rest: 0, Positive: true, name);
            return true;
        }
        return false;
    }
}
