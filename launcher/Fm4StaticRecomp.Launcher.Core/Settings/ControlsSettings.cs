// Copyright (c) 2026, i2kiselev
// BSD 3-Clause License: see LICENSE in the repository root.
using System.Globalization;

namespace Fm4StaticRecomp.Launcher.Core.Settings;

// controls.toml as the panel edits it. Edits are held until Save, which writes only the rows
// that were edited: a value equal to the game's default removes its line (so a later release's
// new default reaches players who never touched the row), anything else sets it, and every
// other line stays as it was.
public sealed class ControlsSettings(string tomlPath)
{
    public const double RampLow = 0.02, RampHigh = 0.60;
    public const int MouseLookLow = 10, MouseLookHigh = 100;

    // A primary key and an optional secondary one. A hand-edited file may list more: they are shown and kept, not added.
    public const int MaxKeys = 2;

    private sealed record Edit(string Value, string Default);

    public sealed record KeysTaken(IReadOnlyList<string> By, IReadOnlyList<string> Lost, IReadOnlyList<string> Kept);

    private readonly Dictionary<string, Edit> _edited = [];
    private TomlFile _file = TomlFile.Load(tomlPath);

    public bool FileIsMalformed => _file.IsMalformed;

    public bool Changed => _edited.Count > 0;

    public IReadOnlyList<string> Keys(ControlSet set, string id) =>
        _edited.TryGetValue(ControlsCatalog.FileKey(set, id), out var edit) ? KeyNames.Split(edit.Value) : SavedKeys(set, id);

    public void SetKey(ControlSet set, string id, int index, string key)
    {
        if (!KeyNames.IsKey(key)) throw new ArgumentException($"{key} is not a key the game can bind.", nameof(key));
        var keys = Keys(set, id).ToList();
        var other = keys.IndexOf(key);
        if (other >= 0 && other != index) keys[other] = keys[index];
        keys[index] = key;
        Store(set, id, keys);
    }

    public void AddKey(ControlSet set, string id, string key)
    {
        if (!KeyNames.IsKey(key)) throw new ArgumentException($"{key} is not a key the game can bind.", nameof(key));
        var keys = Keys(set, id).ToList();
        if (keys.Contains(key)) return;
        if (keys.Count >= MaxKeys) throw new InvalidOperationException($"A row has at most {MaxKeys} keys.");
        keys.Add(key);
        Store(set, id, keys);
    }

    public void RemoveKey(ControlSet set, string id, int index)
    {
        var keys = Keys(set, id).ToList();
        if (keys.Count <= 1) throw new InvalidOperationException("A row keeps at least one key.");
        keys.RemoveAt(index);
        Store(set, id, keys);
    }

    public void Reset(ControlSet set, string id) => Store(set, id, KeyNames.Split(ControlsCatalog.DefaultKeys(set, id)));

    public void ResetAll(ControlSet set)
    {
        foreach (var row in ControlsCatalog.Rows(set)) Reset(set, row.Id);
    }

    public bool IsDefault(ControlSet set, string id) =>
        string.Join(",", Keys(set, id)) == ControlsCatalog.DefaultKeys(set, id);

    // Whether the game reads this row as the player's own (src\input\controls.cpp LoadSet): a line it
    // can use, or an edit here that is not simply the default again.
    public bool IsExplicit(ControlSet set, string id) =>
        _edited.TryGetValue(ControlsCatalog.FileKey(set, id), out var edit) ? edit.Value != edit.Default : StoredKeys(set, id).Count > 0;

    // Labels of the other rows that one key press fires together with this one, which the game leaves
    // alone: two rows you set on the same key, or a key held as a modifier (Shift+W also fires a bare
    // Shift). It does not match modifiers exactly (mnk_exact_modifiers is off), so a bind fires whenever
    // its keys are held. A default row that has a key you gave another row is not listed: the game takes
    // that key off it at start (see TakenBy).
    public IReadOnlyList<string> ConflictsWith(ControlSet set, string id)
    {
        var mine = Keys(set, id);
        var mineExplicit = IsExplicit(set, id);
        return ControlsCatalog.Rows(set)
            .Where(r => r.Id != id && Conflict(mine, mineExplicit, Keys(set, r.Id), IsExplicit(set, r.Id)))
            .Select(r => r.Label).ToList();
    }

    // For a default row: the rows you set that have one of its keys (By), the keys of this row they take
    // (Lost) and the keys it keeps. The game takes the lost keys off this row when it starts and, if none
    // is left, gives it another one.
    public KeysTaken TakenFrom(ControlSet set, string id)
    {
        if (IsExplicit(set, id)) return new([], [], Keys(set, id));
        var mine = Keys(set, id);
        var takers = ControlsCatalog.Rows(set).Where(r => r.Id != id && IsExplicit(set, r.Id)).ToList();
        var lost = mine.Where(k => takers.Any(r => SameKey([k], Keys(set, r.Id)))).ToList();
        var by = takers.Where(r => SameKey(lost, Keys(set, r.Id))).Select(r => r.Label).ToList();
        return new(by, lost, mine.Except(lost).ToList());
    }

    public double Ramp(string key) =>
        _edited.TryGetValue(key, out var edit) ? ParseRamp(edit.Value)!.Value : SavedRamp(key);

    public void SetRamp(string key, double seconds)
    {
        var row = RampRowOf(key);
        StoreRamp(row, Math.Round(Math.Clamp(seconds, RampLow, RampHigh), 2));
    }

    public void ResetRamp(string key)
    {
        var row = RampRowOf(key);
        StoreRamp(row, row.Default);
    }

    public bool RampIsDefault(string key) => FormatRamp(Ramp(key)) == FormatRamp(RampRowOf(key).Default);

    public bool MouseLook
    {
        get => _edited.TryGetValue(ControlsCatalog.MouseLookKey, out var edit) ? edit.Value == "true" : SavedMouseLook();
        set => StoreValue(ControlsCatalog.MouseLookKey, OnOff(value), OnOff(SavedMouseLook()), OnOff(ControlsCatalog.MouseLookDefault));
    }

    public int MouseLookSensitivity =>
        _edited.TryGetValue(ControlsCatalog.MouseLookSensitivityKey, out var edit)
            ? int.Parse(edit.Value, CultureInfo.InvariantCulture) : SavedMouseLookSensitivity();

    public void SetMouseLookSensitivity(int sensitivity) =>
        StoreValue(ControlsCatalog.MouseLookSensitivityKey, Whole(Math.Clamp(sensitivity, MouseLookLow, MouseLookHigh)),
                   Whole(SavedMouseLookSensitivity()), Whole(ControlsCatalog.MouseLookSensitivityDefault));

    public void ResetMouseLook()
    {
        MouseLook = ControlsCatalog.MouseLookDefault;
        SetMouseLookSensitivity(ControlsCatalog.MouseLookSensitivityDefault);
    }

    public bool MouseLookIsDefault =>
        MouseLook == ControlsCatalog.MouseLookDefault
        && MouseLookSensitivity == ControlsCatalog.MouseLookSensitivityDefault;

    // The game may have written the file since it was read: only this session's edits are applied on top.
    public void Save()
    {
        if (!Changed) return;
        var file = TomlFile.Load(tomlPath);
        foreach (var (key, edit) in _edited)
        {
            if (edit.Value == edit.Default) file.Remove(key);
            else file.Set(key, edit.Value, quoted: key.StartsWith("driving_", StringComparison.Ordinal) || key.StartsWith("menu_", StringComparison.Ordinal));
        }
        file.Save(tomlPath);
        _file = file;
        _edited.Clear();
    }

    // 0.3.2 is the first release with the menu defaults the panel shows and the game's own moving of a
    // default key you bind elsewhere (#59); an unreadable version is assumed to.
    public static bool Supports(string version) => !Version.TryParse(version, out var parsed) || parsed >= new Version(0, 3, 2);

    private List<string> StoredKeys(ControlSet set, string id) =>
        _file.Get(ControlsCatalog.FileKey(set, id)) is { } stored ? KeyNames.Split(stored).Where(KeyNames.IsBind).ToList() : [];

    private List<string> SavedKeys(ControlSet set, string id) =>
        StoredKeys(set, id) is { Count: > 0 } keys ? keys : KeyNames.Split(ControlsCatalog.DefaultKeys(set, id));

    private void Store(ControlSet set, string id, List<string> keys)
    {
        var file = ControlsCatalog.FileKey(set, id);
        var value = string.Join(",", keys);
        if (value == string.Join(",", SavedKeys(set, id))) _edited.Remove(file);
        else _edited[file] = new Edit(value, ControlsCatalog.DefaultKeys(set, id));
    }

    private void StoreRamp(RampRow row, double seconds)
    {
        var value = FormatRamp(seconds);
        if (value == FormatRamp(SavedRamp(row.Key))) _edited.Remove(row.Key);
        else _edited[row.Key] = new Edit(value, FormatRamp(row.Default));
    }

    private void StoreValue(string key, string value, string saved, string defaultValue)
    {
        if (value == saved) _edited.Remove(key);
        else _edited[key] = new Edit(value, defaultValue);
    }

    // The game's rules (src\input\controls.cpp): "true" or "false" only; a finite sensitivity above
    // zero, held to the range and rounded half away from zero as std::lround does.
    private bool SavedMouseLook() =>
        _file.Get(ControlsCatalog.MouseLookKey) switch { "true" => true, "false" => false, _ => ControlsCatalog.MouseLookDefault };

    private int SavedMouseLookSensitivity() =>
        _file.Get(ControlsCatalog.MouseLookSensitivityKey) is { } stored && ParseRamp(stored) is { } value
        && double.IsFinite(value) && value > 0
            ? (int)Math.Round(Math.Clamp(value, MouseLookLow, MouseLookHigh), MidpointRounding.AwayFromZero)
            : ControlsCatalog.MouseLookSensitivityDefault;

    private static string OnOff(bool on) => on ? "true" : "false";

    private static string Whole(int value) => value.ToString(CultureInfo.InvariantCulture);

    private double SavedRamp(string key) =>
        _file.Get(key) is { } stored && ParseRamp(stored) is { } seconds ? seconds : RampRowOf(key).Default;

    private static bool Conflict(IReadOnlyList<string> mine, bool mineExplicit, IReadOnlyList<string> other, bool otherExplicit)
    {
        if (!mineExplicit && !otherExplicit) return false;
        var sameKey = SameKey(mine, other);
        if (mineExplicit && otherExplicit && sameKey) return true;
        if (sameKey) return false;
        var held = mine.Select(Pressed).ToList();
        return other.Select(Pressed).Any(o => held.Any(m => m.IsSubsetOf(o) || o.IsSubsetOf(m)));
    }

    // The game's own test (BareKey in LoadSet): the same key once modifiers are ignored.
    private static bool SameKey(IReadOnlyList<string> a, IReadOnlyList<string> b) => a.Any(x => b.Any(y => BareKey(x) == BareKey(y)));

    private static string BareKey(string token)
    {
        var plus = token.LastIndexOf('+');
        return plus < 0 || plus + 1 == token.Length ? token : token[(plus + 1)..];
    }

    // The keys held down to fire a bind, as the game names them once: Ctrl is Control, NumpadEnter is Return.
    private static HashSet<string> Pressed(string bind) =>
        bind.Split('+').Select(part => part switch { "Ctrl" => "Control", "NumpadEnter" => "Return", _ => part }).ToHashSet();

    private static RampRow RampRowOf(string key) =>
        ControlsCatalog.Ramps.FirstOrDefault(r => r.Key == key) ?? throw new ArgumentException($"{key} is not a ramp.", nameof(key));

    // The game accepts any number of seconds >= 0 (LoadRate in src\input\controls.cpp).
    private static double? ParseRamp(string text) =>
        double.TryParse(text, NumberStyles.Float, CultureInfo.InvariantCulture, out var seconds) && seconds >= 0 ? seconds : null;

    private static string FormatRamp(double seconds) => seconds.ToString("0.##", CultureInfo.InvariantCulture);
}
