// Copyright (c) 2026, i2kiselev
// BSD 3-Clause License: see LICENSE in the repository root.
namespace Fm4StaticRecomp.Launcher.Core.Settings;

// Pad: the pad button name the SDK's wheel_buttons stores; MenuMeaning: what the same button does in menus.
public sealed record WheelAction(string Id, string Label, string Pad, string? MenuMeaning = null)
{
    public bool IsGate => Pad.StartsWith("GEAR", StringComparison.Ordinal) || Pad == "REVERSE";
}

// The actions of the game's default wheel layout (gamedb WheelLayouts, layout 0), then an H-shifter's gates.
public static class WheelActions
{
    public static IReadOnlyList<WheelAction> All { get; } =
    [
        new("shift_up", "Shift up", "RB"),
        new("shift_down", "Shift down", "LB"),
        new("clutch", "Clutch", "A", "select"),
        new("handbrake", "Handbrake", "B", "back"),
        new("rewind", "Rewind", "X"),
        new("look_back", "Look back", "Y"),
        new("start", "Pause", "START"),
        new("back", "Back", "BACK"),
        .. Enumerable.Range(1, 7).Select(n => new WheelAction($"gear{n}", $"Gear {n}", $"GEAR{n}")),
        new("reverse", "Reverse", "REVERSE"),
    ];

    public static WheelAction Of(string id) =>
        All.FirstOrDefault(a => a.Id == id) ?? throw new ArgumentException($"{id} is not a wheel action.", nameof(id));
}
