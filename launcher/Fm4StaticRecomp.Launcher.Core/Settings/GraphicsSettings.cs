namespace Fm4StaticRecomp.Launcher.Core.Settings;

public sealed class GraphicsSettings(GraphicsSchema schema, string tomlPath, string backupsFolder)
{
    private const string Scale = "resolution_scale", Upscaler = "present_effect";

    private readonly Dictionary<string, string> _changed = [];
    private TomlFile _file = TomlFile.Load(tomlPath);
    private bool _backedUp;

    public GraphicsSchema Schema => schema;
    public bool FileIsMalformed => _file.IsMalformed;

    public string Value(string key)
    {
        if (_changed.TryGetValue(key, out var changed)) return changed;
        var row = Row(key);
        var stored = _file.Get(key);
        return stored is not null && row.Accepts(stored) ? stored : row.Default ?? "";
    }

    public void Set(string key, string value)
    {
        if (!Row(key).Accepts(value)) throw new ArgumentException($"{key} does not accept {value}.");
        _changed[key] = value;
    }

    public bool Shows(SchemaRow row) => row.ShownWhen is null || row.ShownWhen.All(c => Value(c.Key) == c.Value);

    public string? CurrentPreset() =>
        schema.Presets.FirstOrDefault(p => p.Values.All(v => Value(v.Key) == v.Value))?.Label;

    public void ApplyPreset(string label)
    {
        foreach (var (key, value) in schema.Presets.Single(p => p.Label == label).Values) Set(key, value);
    }

    public int FrameRateCeiling(int? displayHz)
    {
        var row = Row("fm4_frame_rate");
        return displayHz is > 0 ? Math.Clamp(displayHz.Value, row.Low, row.High) : row.High;
    }

    // The game runs an upscaler only while the render (1280x720 x scale) fits the
    // display (src\ui\graphics_page.cpp LargestFittingScale); 0 when no scale fits, null while
    // the display size is unknown.
    public static int? LargestFittingScale((int Width, int Height)? display) =>
        display is { Width: > 0, Height: > 0 } size ? Math.Min(size.Width / 1280, size.Height / 720) : null;

    // A version built before the UPSCALER row (0.2.2, still active while an update builds or
    // after GO BACK TO) has none.
    private bool UpscalerChosen => schema.SettingRows.Any(r => r.Key == Upscaler) && Value(Upscaler) != "bilinear";

    // With an upscaler chosen, only the scales it runs at, plus the current one if it is larger.
    public IReadOnlyList<SchemaChoice> ScaleChoices((int Width, int Height)? display)
    {
        var choices = Row(Scale).Choices!;
        if (!UpscalerChosen || LargestFittingScale(display) is not (> 0 and var largest)) return choices;
        var current = Value(Scale);
        return choices.Where(c => int.Parse(c.Value) <= largest || c.Value == current).ToList();
    }

    // Choosing an upscaler over a scale it does not run at lowers the scale; the lowered scale, or null.
    public int? FitScaleToUpscaler((int Width, int Height)? display)
    {
        if (!UpscalerChosen || LargestFittingScale(display) is not (> 0 and var largest)
            || int.Parse(Value(Scale)) <= largest) return null;
        Set(Scale, largest.ToString());
        return largest;
    }

    // The upscaler that will run, as its row names it. Null for none.
    public string? RunningUpscaler((int Width, int Height)? display)
    {
        if (!UpscalerChosen) return null;
        if (LargestFittingScale(display) is { } largest && int.Parse(Value(Scale)) > largest) return null;
        return Row(Upscaler).Choices!.Single(c => c.Value == Value(Upscaler)).Label;
    }

    // The running upscaler that takes over MSAA and post AA; FSR 1 works on the finished
    // picture and leaves them on. Null for none.
    public string? TemporalUpscaler((int Width, int Height)? display) =>
        RunningUpscaler(display) is { } upscaler && Value(Upscaler) != "fsr" ? upscaler : null;

    // The game may have written the file since it was read: only this session's changes are
    // applied on top of what is there now.
    public void Save()
    {
        if (_changed.Count == 0) return;
        if (!_backedUp && File.Exists(tomlPath))
        {
            Directory.CreateDirectory(backupsFolder);
            File.Copy(tomlPath, NewBackupPath(), overwrite: false);
            _backedUp = true;
        }
        _file = TomlFile.Load(tomlPath);
        foreach (var (key, value) in _changed) _file.Set(key, value, Row(key).String);
        _file.Save(tomlPath);
        _changed.Clear();
    }

    public void ResetToDefaults()
    {
        foreach (var row in schema.SettingRows.Where(r => r.Default is not null)) _changed[row.Key!] = row.Default!;
    }

    public bool RestoreBackup()
    {
        var newest = Directory.Exists(backupsFolder)
            ? Directory.GetFiles(backupsFolder, "graphics-*.toml").Max(StringComparer.Ordinal)
            : null;
        if (newest is null) return false;
        File.Copy(newest, tomlPath, overwrite: true);
        _changed.Clear();
        _file = TomlFile.Load(tomlPath);
        return true;
    }

    // The game's own name for a backup (src\config\host_config.cpp), so the newest of both
    // writers sorts last.
    private string NewBackupPath()
    {
        var stamp = DateTime.UtcNow;
        string path;
        while (File.Exists(path = Path.Combine(backupsFolder, $"graphics-{stamp:yyyyMMdd'T'HHmmssfff'Z'}.toml")))
            stamp = stamp.AddMilliseconds(1);
        return path;
    }

    private SchemaRow Row(string key) =>
        schema.SettingRows.FirstOrDefault(r => r.Key == key) ?? throw new KeyNotFoundException(key);
}
