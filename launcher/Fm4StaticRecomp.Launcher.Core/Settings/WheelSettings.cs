// Copyright (c) 2026, FugaziScript
// BSD 3-Clause License: see LICENSE in the repository root.
using System.Globalization;

namespace Fm4StaticRecomp.Launcher.Core.Settings;

// config\wheel.toml as the WHEEL panel edits it: the game's fm4_wheel_* settings
// (src\wheel\wheel_settings.cpp), written as they change. Keys here drop the fm4_wheel_ prefix.
public sealed class WheelSettings(string tomlPath)
{
    public const string Prefix = "fm4_wheel_";

    // The game's defaults (src\wheel\wheel_settings.cpp).
    public static readonly IReadOnlyDictionary<string, string> Defaults = new Dictionary<string, string>
    {
        ["enabled"] = "false", ["device"] = "", ["steering_axis"] = "0", ["steering_invert"] = "false",
        ["steering_scale"] = "100", ["steering_deadzone"] = "0", ["rotation"] = "900", ["game_rotation"] = "0",
        ["throttle"] = "", ["brake"] = "", ["clutch"] = "", ["clutch_button"] = "B",
        ["buttons"] = "UP=h0.up,DOWN=h0.down,LEFT=h0.left,RIGHT=h0.right",
        ["pedal_deadzone"] = "3",
        ["throttle_deadzone_bottom"] = "-1", ["throttle_deadzone_top"] = "0",
        ["brake_deadzone_bottom"] = "-1", ["brake_deadzone_top"] = "0",
        ["clutch_deadzone_bottom"] = "-1", ["clutch_deadzone_top"] = "0",
        ["ffb_device"] = "", ["ffb_mode"] = "on", ["ffb_strength"] = "25", ["ffb_min_force"] = "0",
        ["ffb_spring"] = "100", ["ffb_damper"] = "100", ["ffb_friction"] = "100", ["ffb_inertia"] = "100",
        ["ffb_rumble"] = "50", ["ffb_rumble_engine"] = "100", ["ffb_rumble_road"] = "100",
        ["ffb_invert"] = "false", ["log_inputs"] = "false",
    };

    private static readonly HashSet<string> Strings =
        ["device", "throttle", "brake", "clutch", "clutch_button", "buttons", "ffb_device", "ffb_mode"];

    // Pad controls and H-shifter gates a wheel input can press (the SDK's wheel_buttons names).
    public static readonly IReadOnlyList<string> ButtonNames =
    [
        "A", "B", "X", "Y", "LB", "RB", "LT", "RT", "LS", "RS", "BACK", "START", "UP", "DOWN", "LEFT", "RIGHT",
        "GEAR1", "GEAR2", "GEAR3", "GEAR4", "GEAR5", "GEAR6", "GEAR7", "REVERSE",
    ];

    private TomlFile _file = TomlFile.Load(tomlPath);

    public bool FileIsMalformed => _file.IsMalformed;

    public string Get(string key)
    {
        if (!Defaults.TryGetValue(key, out var fallback)) throw new KeyNotFoundException(key);
        return _file.Get(Prefix + key) ?? fallback;
    }

    public bool GetBool(string key) => Get(key) == "true";

    public int GetInt(string key) =>
        int.TryParse(Get(key), NumberStyles.Integer, CultureInfo.InvariantCulture, out var value)
            ? value : int.Parse(Defaults[key], CultureInfo.InvariantCulture);

    // Writes at once: the game reads the file at its next start, so there is nothing to apply.
    public void Set(string key, string value)
    {
        if (!Defaults.ContainsKey(key)) throw new KeyNotFoundException(key);
        _file = TomlFile.Load(tomlPath);
        _file.Set(Prefix + key, value, Strings.Contains(key));
        _file.Save(tomlPath);
    }

    public void Set(string key, int value) => Set(key, value.ToString(CultureInfo.InvariantCulture));

    public void Set(string key, bool value) => Set(key, value ? "true" : "false");

    // fm4_wheel_buttons as NAME=input pairs, in order; a name may have several inputs.
    public IReadOnlyList<(string Name, string Input)> Buttons() => ParseButtons(Get("buttons"));

    public static IReadOnlyList<(string Name, string Input)> ParseButtons(string text) =>
        text.Split(',', StringSplitOptions.RemoveEmptyEntries | StringSplitOptions.TrimEntries)
            .Select(entry => entry.Split('=', 2))
            .Where(parts => parts.Length == 2 && parts[0].Length > 0 && parts[1].Length > 0)
            .Select(parts => (parts[0].ToUpperInvariant(), parts[1]))
            .ToList();

    public static string FormatButtons(IEnumerable<(string Name, string Input)> buttons) =>
        string.Join(",", buttons.Select(b => $"{b.Name}={b.Input}"));

    // Binds `input` to `name`: replacing what it had, or as one more input when `add`.
    public void BindButton(string name, string input, bool add)
    {
        var buttons = Buttons().ToList();
        if (!add) buttons.RemoveAll(b => b.Name == name);
        if (!buttons.Contains((name, input))) buttons.Add((name, input));
        Set("buttons", FormatButtons(buttons));
    }

    public void ClearButton(string name) =>
        Set("buttons", FormatButtons(Buttons().Where(b => b.Name != name)));

    // The steering sensitivity the game applies (src\wheel\wheel_mapping.cpp SteeringScale).
    public static int SteeringScale(int scale, int wheelRotation, int gameRotation)
    {
        long result = Math.Clamp(scale, 10, 1000);
        if (gameRotation > 0 && wheelRotation > 0) result = result * wheelRotation / gameRotation;
        return (int)Math.Clamp(result, 10, 1000);
    }
}
