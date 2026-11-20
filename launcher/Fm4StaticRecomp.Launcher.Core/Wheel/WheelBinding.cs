// Copyright (c) 2026, i2kiselev
// BSD 3-Clause License: see LICENSE in the repository root.
using System.Globalization;

namespace Fm4StaticRecomp.Launcher.Core.Wheel;

// One wheel.toml binding as the SDK reads it (src/input/sdl/wheel_mapping.cpp): "[device|][+|-]axisN",
// "[device|][b]N" or "[device|]hN.up/down/left/right". An empty device is the wheel base.
public sealed record WheelBinding(string Device, char? Sign, CaptureKind Kind, int Index, string? HatDirection)
{
    private static readonly string[] HatNames = ["up", "down", "left", "right"];

    public static WheelBinding? Parse(string text)
    {
        var bar = text.LastIndexOf('|');
        var device = bar < 0 ? "" : text[..bar].Trim();
        var rest = text[(bar + 1)..].Trim();
        char? sign = rest.Length > 0 && rest[0] is '+' or '-' ? rest[0] : null;
        if (sign is not null) rest = rest[1..];
        if (sign is not null || rest.StartsWith('a') || rest.StartsWith('A'))
        {
            rest = rest.StartsWith("axis", StringComparison.OrdinalIgnoreCase) ? rest[4..] : rest.Length > 0 ? rest[1..] : rest;
            return Number(rest) is { } axis ? new(device, sign, CaptureKind.Axis, axis, null) : null;
        }
        if (rest.StartsWith('h') || rest.StartsWith('H'))
        {
            var dot = rest.IndexOf('.');
            if (dot < 0 || Number(rest[1..dot]) is not { } hat) return null;
            var direction = rest[(dot + 1)..].Trim().ToLowerInvariant();
            return HatNames.Contains(direction) ? new(device, null, CaptureKind.Hat, hat, direction) : null;
        }
        if (rest.StartsWith('b') || rest.StartsWith('B')) rest = rest[1..];
        return Number(rest) is { } button ? new(device, null, CaptureKind.Button, button, null) : null;
    }

    // How far a pedal is pressed, 0 to 100. Without a sign the SDK reads a pedal that rests above
    // the middle as running down (ResolveAxisDirection).
    public static int PedalPercent(short raw, short rest, char? sign)
    {
        var travel = raw + 32768;
        if (sign == '-' || (sign is null && rest > 0)) travel = 65535 - travel;
        return (int)Math.Round(travel * 100.0 / 65535);
    }

    private static int? Number(string text) =>
        int.TryParse(text, NumberStyles.None, CultureInfo.InvariantCulture, out var index) ? index : null;
}
