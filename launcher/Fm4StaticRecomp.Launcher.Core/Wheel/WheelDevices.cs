// Portions copyright (c) 2026, Pinyon Shift contributors (BSD 3-Clause: see THIRD_PARTY_NOTICES.md).
// Copyright (c) 2026, i2kiselev (BSD 3-Clause: see LICENSE in the repository root).
// Adapted from Pinyon Shift's launcher (launcher/PinyonShift.Launcher/Wheel/Input/WheelMath.cs, BSD-3-Clause; see THIRD_PARTY_NOTICES.md).
using System.Globalization;

namespace Fm4StaticRecomp.Launcher.Core.Wheel;

public static class WheelDevices
{
    // The SDK's device match (WheelDeviceMatches): "vvvv:pppp", "path:<path>" or part of the name.
    public static bool Matches(string match, ProbeDevice device)
    {
        match = match.Trim();
        if (match.Length == 0) return true;
        if (match.StartsWith("path:", StringComparison.OrdinalIgnoreCase))
            return device.Path.Length > 0 && string.Equals(match[5..].Trim(), device.Path, StringComparison.OrdinalIgnoreCase);
        if (match.Length == 9 && match[4] == ':' &&
            ushort.TryParse(match[..4], NumberStyles.AllowHexSpecifier, CultureInfo.InvariantCulture, out var vendor) &&
            ushort.TryParse(match[5..], NumberStyles.AllowHexSpecifier, CultureInfo.InvariantCulture, out var product))
            return vendor == device.Vendor && product == device.Product;
        return device.Name.Contains(match, StringComparison.OrdinalIgnoreCase);
    }

    // The shortest text that names exactly this device among those connected; two identical pedal
    // sets or wheels share their USB ids and name, so they get their paths.
    public static string Match(ProbeDevice device, IReadOnlyList<ProbeDevice> all)
    {
        if ((device.Vendor != 0 || device.Product != 0)
            && all.Count(d => d.Vendor == device.Vendor && d.Product == device.Product) <= 1)
            return device.Ids;
        var name = device.Name;
        if (name.Length > 0 && name.IndexOfAny(['|', ',', '=']) < 0
            && all.Count(d => d.Name.Contains(name, StringComparison.OrdinalIgnoreCase)) <= 1)
            return name;
        return "path:" + device.Path;
    }

    public static ProbeDevice? Find(string match, IReadOnlyList<ProbeDevice> all) => all.FirstOrDefault(d => Matches(match, d));

    // The steering device with wheel_device empty (the SDK's IsSteeringCandidate): a wheel maker's
    // pedal sets, shifters and handbrakes share its vendor id, so their names rule them out.
    public static ProbeDevice? SteeringCandidate(IReadOnlyList<ProbeDevice> all) =>
        all.FirstOrDefault(d => d.Wheel && !new[] { "pedal", "shifter", "handbrake" }.Any(part =>
            d.Name.Contains(part, StringComparison.OrdinalIgnoreCase)));
}
