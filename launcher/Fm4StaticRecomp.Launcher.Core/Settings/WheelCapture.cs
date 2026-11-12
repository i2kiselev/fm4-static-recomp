// Copyright (c) 2026, FugaziScript
// BSD 3-Clause License: see LICENSE in the repository root.
namespace Fm4StaticRecomp.Launcher.Core.Settings;

// BIND on the WHEEL panel: the first input that moves after the click, written as a binding
// ("[device|]input", src\wheel\wheel_mapping.h). An axis counts once it has moved past a third of its
// travel from where it was, so a resting pedal's noise or a light touch on the wheel binds nothing.
public sealed class WheelCapture(WheelSnapshot start, bool axis)
{
    private const int AxisTravel = 65535 / 3;

    // The binding, or null while nothing has moved. `axis` captures take only axes (steering, pedals);
    // the others take buttons, hat directions and axes as buttons (a handbrake lever).
    public string? Next(WheelSnapshot now)
    {
        foreach (var device in now.Devices)
        {
            var before = start.Devices.FirstOrDefault(d => d.Path == device.Path && d.Name == device.Name);
            if (before is null) continue;
            for (var i = 0; i < Math.Min(device.Axes.Count, before.Axes.Count); i++)
            {
                var moved = device.Axes[i] - before.Axes[i];
                if (Math.Abs(moved) < AxisTravel) continue;
                var input = axis ? $"axis{i}" : $"{(moved > 0 ? "+" : "-")}axis{i}";
                return Bind(device, now, input);
            }
            if (axis) continue;
            for (var i = 0; i < Math.Min(device.Buttons.Count, before.Buttons.Count); i++)
                if (device.Buttons[i] && !before.Buttons[i]) return Bind(device, now, i.ToString(System.Globalization.CultureInfo.InvariantCulture));
            for (var i = 0; i < Math.Min(device.Hats.Count, before.Hats.Count); i++)
                if (device.Hats[i] != 0 && before.Hats[i] == 0 && HatDirection(device.Hats[i]) is { } direction)
                    return Bind(device, now, $"h{i}.{direction}");
        }
        return null;
    }

    // An axis's index, for the steering row, which reads only the wheel base.
    public static int? AxisIndex(string binding) =>
        binding.Split('|')[^1].TrimStart('+', '-') is var input && input.StartsWith("axis", StringComparison.Ordinal)
        && int.TryParse(input[4..], System.Globalization.NumberStyles.Integer, System.Globalization.CultureInfo.InvariantCulture, out var index)
            ? index : null;

    // Whether a pedal binding reads reversed: "-axisN", or without a sign an axis resting in its
    // upper half (the game's ResolveAxisDirection). `rest` is the axis's resting value, when known.
    public static bool IsReversed(string binding, int? rest)
    {
        var input = binding.Split('|')[^1];
        return input.StartsWith('-') || (!input.StartsWith('+') && rest > 0);
    }

    // The same pedal travelling the other way: released becomes fully pressed. The sign is written
    // out, so the direction no longer depends on where the pedal rests.
    public static string Reverse(string binding, int? rest)
    {
        if (AxisIndex(binding) is not { } index) return binding;
        var device = DeviceOf(binding);
        var input = $"{(IsReversed(binding, rest) ? "+" : "-")}axis{index}";
        return device.Length > 0 ? $"{device}|{input}" : input;
    }

    // The device of a binding: empty for the wheel base.
    public static string DeviceOf(string binding) => binding.Contains('|') ? binding[..binding.LastIndexOf('|')] : "";

    private static string Bind(WheelDevice device, WheelSnapshot now, string input) =>
        device.Match == now.Base ? input : $"{device.Match}|{input}";

    private static string? HatDirection(int hat) => hat switch
    {
        1 => "up", 2 => "right", 4 => "down", 8 => "left", _ => null,
    };
}
