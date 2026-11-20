// Copyright (c) 2026, i2kiselev
// BSD 3-Clause License: see LICENSE in the repository root.
using System.Globalization;
using System.Text.RegularExpressions;

namespace Fm4StaticRecomp.Launcher.Core.Settings;

public enum WheelAxis { Steering, Throttle, Brake, Clutch, Handbrake }

// wheel.toml as the WHEEL screen edits it; the game's PC OPTIONS → WHEEL writes the same file.
// The rules follow src\wheel_settings.cpp. Edits are held until Save, which applies only them on
// top of the file as it is then, so lines the game wrote meanwhile stay.
public sealed class WheelSettings(string tomlPath, WheelSchema schema, string backupsFolder)
{
    private const string TypeKey = "wheel_type", Custom = "custom", RotationKey = "wheel_rotation", ScaleKey = "wheel_steering_scale",
        ButtonsKey = "wheel_buttons", DeviceKey = "wheel_device", SteeringAxisKey = "wheel_steering_axis",
        SteeringInvertKey = "wheel_steering_invert", EnabledKey = "wheel_enabled";

    public const int DefaultRotation = 900, MinSteeringLockDegrees = 180;
    // wheel_steering_scale's range in the SDK.
    private const int MinScale = 10, MaxScale = 1000;

    // A null value removes the line.
    private readonly Dictionary<string, string?> _edited = [];
    private TomlFile _file = TomlFile.Load(tomlPath);
    private bool _backedUp;

    public WheelSchema Schema => schema;
    public bool FileIsMalformed => _file.IsMalformed;
    public bool Changed => _edited.Any(e => e.Value != _file.Get(e.Key));

    public bool Enabled
    {
        get => Read(EnabledKey) == "true";
        set => Store(EnabledKey, value ? "true" : null);
    }

    public static string AxisKey(WheelAxis axis) => axis switch
    {
        WheelAxis.Throttle => "wheel_throttle",
        WheelAxis.Brake => "wheel_brake",
        WheelAxis.Clutch => "wheel_clutch",
        WheelAxis.Handbrake => "wheel_handbrake",
        _ => throw new ArgumentException("Steering is wheel_device and wheel_steering_axis.", nameof(axis)),
    };

    // "[device|][+|-]axisN" as the SDK reads it; null when not set.
    public string? Axis(WheelAxis axis)
    {
        if (axis != WheelAxis.Steering) return Read(AxisKey(axis)) is { Length: > 0 } binding ? binding : null;
        var device = Read(DeviceKey) ?? "";
        var index = Read(SteeringAxisKey);
        if (device.Length == 0 && index is null) return null;
        return (device.Length > 0 ? device + "|" : "") + "axis" + (index ?? "0");
    }

    public void SetAxis(WheelAxis axis, string? binding)
    {
        if (axis != WheelAxis.Steering)
        {
            Store(AxisKey(axis), binding is { Length: > 0 } ? binding : null);
            return;
        }
        if (binding is null)
        {
            Store(DeviceKey, null);
            Store(SteeringAxisKey, null);
            return;
        }
        var bar = binding.LastIndexOf('|');
        var device = bar < 0 ? "" : binding[..bar].Trim();
        var index = binding[(bar + 1)..].Trim();
        if (index.StartsWith("axis", StringComparison.OrdinalIgnoreCase)) index = index[4..];
        if (!int.TryParse(index, NumberStyles.None, CultureInfo.InvariantCulture, out var n))
            throw new ArgumentException($"{binding} is not an axis.", nameof(binding));
        Store(DeviceKey, device.Length > 0 ? device : null);
        Store(SteeringAxisKey, n.ToString(CultureInfo.InvariantCulture));
    }

    public bool SteeringInvert
    {
        get => Read(SteeringInvertKey) == "true";
        set => Store(SteeringInvertKey, value ? "true" : null);
    }

    // The source ("[device|]N" or "[device|]hN.dir") bound to an action's pad button; null when none.
    public string? Button(string actionId)
    {
        var pad = WheelActions.Of(actionId).Pad;
        return ButtonEntries().FirstOrDefault(e => e.Name.Equals(pad, StringComparison.OrdinalIgnoreCase)).Inputs?.Split(',')[0];
    }

    // Entries for pad names this launcher does not list stay as they are.
    public void SetButton(string actionId, string? source)
    {
        var pad = WheelActions.Of(actionId).Pad;
        var entries = ButtonEntries();
        var at = entries.FindIndex(e => e.Name.Equals(pad, StringComparison.OrdinalIgnoreCase));
        entries.RemoveAll(e => e.Name.Equals(pad, StringComparison.OrdinalIgnoreCase));
        if (source is { Length: > 0 }) entries.Insert(at < 0 ? entries.Count : at, (pad, source));
        var text = string.Join(",", entries.Select(e => $"{e.Name}={e.Inputs}"));
        Store(ButtonsKey, text == ExpertDefault(ButtonsKey) ? null : text);
    }

    public string WheelType
    {
        get
        {
            if (Read(TypeKey) is { } saved && (saved == Custom || schema.Types.Any(t => t.Id == saved))) return saved;
            return schema.Types[0].Values.Keys.Any(k => Read(k) is not null) ? Custom : schema.Types[0].Id;
        }
    }

    public void SetWheelType(string id)
    {
        var type = schema.Types.FirstOrDefault(t => t.Id == id) ?? throw new ArgumentException($"{id} is not a wheel type.", nameof(id));
        foreach (var (key, value) in type.Values) Store(key, Whole(value));
        Store(TypeKey, type.Id);
    }

    // File value clamped, else the wheel type's, else the row's own (WheelValue in the game).
    public int Value(string key)
    {
        if (key == WheelSchema.SteeringLockKey) return SteeringLock;
        var row = Row(key);
        if (ParseInt(Read(key)) is { } saved) return Math.Clamp(saved, row.Min, row.Max);
        if (schema.Types.FirstOrDefault(t => t.Id == WheelType) is { } type && type.Values.TryGetValue(key, out var preset)) return preset;
        return row.Fallback;
    }

    public void SetValue(string key, int value)
    {
        if (key == WheelSchema.SteeringLockKey)
        {
            SteeringLock = value;
            return;
        }
        var row = Row(key);
        Store(key, Whole(Math.Clamp(value, row.Min, row.Max)));
        if (schema.Types[0].Values.ContainsKey(key)) Store(TypeKey, Custom);
    }

    // As set in the wheel's own software; SDL cannot read it.
    public int Rotation
    {
        get => Math.Max(ParseInt(Read(RotationKey)) ?? DefaultRotation, MinSteeringLockDegrees);
        set
        {
            var keepLock = Read(ScaleKey) is not null ? SteeringLock : (int?)null;
            Store(RotationKey, value == DefaultRotation ? null : Whole(Math.Max(value, MinSteeringLockDegrees)));
            if (keepLock is { } degrees) SteeringLock = degrees;
        }
    }

    // The smallest lock wheel_steering_scale reaches at this rotation.
    public int MinSteeringLock => Math.Max((Rotation * 100 / MaxScale + 9) / 10 * 10, MinSteeringLockDegrees);

    // In degrees: wheel_steering_scale = rotation / lock x 100, read back to the nearest 10 degrees.
    public int SteeringLock
    {
        get
        {
            var scale = Math.Clamp(ParseInt(Read(ScaleKey)) ?? 100, MinScale, MaxScale);
            var degrees = (int)Math.Round(Rotation * 10.0 / scale, MidpointRounding.AwayFromZero) * 10;
            return Math.Clamp(degrees, MinSteeringLock, Rotation);
        }
        set
        {
            var degrees = Math.Clamp(value, MinSteeringLock, Rotation);
            var scale = (int)Math.Round(Rotation * 100.0 / degrees, MidpointRounding.AwayFromZero);
            Store(ScaleKey, Whole(Math.Clamp(scale, MinScale, MaxScale)));
        }
    }

    public string Expert(string key) => Read(key) ?? ExpertDefault(key);

    public void SetExpert(string key, string value) => Store(key, value == ExpertDefault(key) ? null : value);

    public sealed class Edits(Dictionary<string, string?> values)
    {
        internal Dictionary<string, string?> Values { get; } = values;
    }

    // The unsaved edits now, to go back to when the guided set-up is cancelled.
    public Edits Mark() => new(new Dictionary<string, string?>(_edited));

    public void Revert(Edits mark)
    {
        _edited.Clear();
        foreach (var (key, value) in mark.Values) _edited[key] = value;
    }

    // 0.4.0 is the first release with wheel.toml and the probe; an unreadable version is assumed to have them.
    public static bool Supports(string version) => !Version.TryParse(version, out var parsed) || parsed >= new Version(0, 4, 0);

    public void Save()
    {
        if (_edited.Count == 0) return;
        if (!_backedUp && File.Exists(tomlPath))
        {
            Directory.CreateDirectory(backupsFolder);
            File.Copy(tomlPath, NewBackupPath(), overwrite: false);
            _backedUp = true;
        }
        var file = TomlFile.Load(tomlPath);
        foreach (var (key, value) in _edited)
        {
            if (value is null) file.Remove(key);
            else file.Set(key, value, Quoted(key));
        }
        file.Save(tomlPath);
        _file = file;
        _edited.Clear();
    }

    private string? Read(string key) => _edited.TryGetValue(key, out var edited) ? edited : _file.Get(key);

    // Kept even when it equals the file as read: the game may have changed the file since, and
    // Save writes what the screen shows.
    private void Store(string key, string? value) => _edited[key] = value;

    // "NAME=input" entries; a part without '=' is one more input of the previous NAME ("A=0,1"),
    // as the SDK reads them (ParseWheelButtonBindings).
    private List<(string Name, string Inputs)> ButtonEntries()
    {
        var entries = new List<(string Name, string Inputs)>();
        foreach (var part in (Read(ButtonsKey) ?? ExpertDefault(ButtonsKey)).Split(',').Select(p => p.Trim()).Where(p => p.Length > 0))
        {
            var equals = part.IndexOf('=');
            if (equals > 0) entries.Add((part[..equals].Trim(), part[(equals + 1)..].Trim()));
            else if (equals < 0 && entries.Count > 0) entries[^1] = (entries[^1].Name, entries[^1].Inputs + "," + part);
        }
        return entries;
    }

    private string ExpertDefault(string key) =>
        schema.Expert.FirstOrDefault(e => e.Key == key)?.Default ?? throw new KeyNotFoundException(key);

    // Numbers and switches go in bare, as the game writes them; everything else is quoted.
    private bool Quoted(string key)
    {
        if (key == TypeKey) return true;
        if (key is RotationKey or ScaleKey || schema.Rows.Any(r => r.Key == key)) return false;
        var fallback = schema.Expert.FirstOrDefault(e => e.Key == key)?.Default;
        return fallback is not null && fallback is not ("true" or "false") && ParseInt(fallback) is null;
    }

    private WheelRow Row(string key) =>
        schema.Rows.FirstOrDefault(r => r.Key == key) ?? throw new KeyNotFoundException(key);

    // As the game's ParseInt (strtol, held to ±1,000,000): a number too long for any integer
    // saturates rather than counting as garbled.
    private static int? ParseInt(string? text)
    {
        const long Limit = 1_000_000;
        if (text?.Trim() is not { } number || !Regex.IsMatch(number, @"^[+-]?[0-9]+$")) return null;
        if (!long.TryParse(number, NumberStyles.AllowLeadingSign, CultureInfo.InvariantCulture, out var value))
            value = number[0] == '-' ? -Limit : Limit;
        return (int)Math.Clamp(value, -Limit, Limit);
    }

    private static string Whole(int value) => value.ToString(CultureInfo.InvariantCulture);

    // The game's own backup name (src\config\host_config.cpp), so the newest of both writers sorts last.
    private string NewBackupPath()
    {
        var stamp = DateTime.UtcNow;
        string path;
        while (File.Exists(path = Path.Combine(backupsFolder, $"wheel-{stamp:yyyyMMdd'T'HHmmssfff'Z'}.toml")))
            stamp = stamp.AddMilliseconds(1);
        return path;
    }
}
