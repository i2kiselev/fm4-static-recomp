// Copyright (c) 2026, i2kiselev
// BSD 3-Clause License: see LICENSE in the repository root.
namespace Fm4StaticRecomp.Launcher.Core.Settings;

public sealed class LauncherSettings
{
    public const int MaximumBuildJobs = 32;
    public const int GameVibrationStrength = 100;
    public const int MaximumVibrationStrength = 200;
    private readonly string _path;
    private readonly TomlFile _file;

    private LauncherSettings(string path)
    {
        _path = path;
        _file = TomlFile.Load(path);
        ActiveProfile = _file.Get("active_profile");
        ActiveVersion = _file.Get("active_version");
        PreviousVersion = _file.Get("previous_version");
        SkipIntro = _file.Get("skip_intro") != "false";
        ShowFps = _file.Get("show_fps") == "true";
        ControllerVibration = _file.Get("controller_vibration") != "false";
        AudioPopFix = _file.Get("audio_pop_fix") != "false";
        AsyncReadbacks = _file.Get("async_readbacks") != "false";
        PresentViaDxgi = _file.Get("present_via_dxgi") == "true";
        Compat = _file.Get("compat") == "true";
        Diagnostic30Fps = _file.Get("diagnostic_30fps") == "true";
        Windowed = _file.Get("windowed") == "true";
        WindowSize = WindowSizes.Parse(_file.Get("window_size"));
        BuildJobs = int.TryParse(_file.Get("build_jobs"), out var jobs) && jobs is >= 0 and <= MaximumBuildJobs ? jobs : 0;
        VibrationStrength = int.TryParse(_file.Get("vibration_strength"), out var vibration)
                            && vibration is >= 0 and <= MaximumVibrationStrength ? vibration : GameVibrationStrength;
        AcceptedMicrosoftLicence = _file.Get("accepted_microsoft_licence");
        UseVisualStudioInstaller = _file.Get("toolchain_source") == "VisualStudio";
        Language = (GameLanguages.Find(_file.Get("language")) ?? GameLanguages.Default).Code;
        CommandLineOptions = _file.Get("command_line_options")?.Trim() ?? "";
        SavesFolder = _file.Get("saves_folder");
    }

    public string? ActiveProfile { get; set; }
    public string? ActiveVersion { get; set; }
    public string? PreviousVersion { get; set; }
    public bool SkipIntro { get; set; }
    public bool ShowFps { get; set; }
    public bool ControllerVibration { get; set; }
    public bool AudioPopFix { get; set; }
    public bool AsyncReadbacks { get; set; }
    public bool PresentViaDxgi { get; set; }
    public bool Compat { get; set; }
    public bool Diagnostic30Fps { get; set; }
    public bool Windowed { get; set; }
    public (int Width, int Height) WindowSize { get; set; }
    public int BuildJobs { get; set; }
    public int VibrationStrength { get; set; }
    public string? AcceptedMicrosoftLicence { get; set; }
    public bool UseVisualStudioInstaller { get; set; }
    public string Language { get; set; }
    public string CommandLineOptions { get; set; }
    // Null: the default, the data folder.
    public string? SavesFolder { get; set; }

    public static LauncherSettings Load(string path) => new(path);

    public void Save()
    {
        void Text(string key, string? value)
        {
            if (value is null) _file.Remove(key);
            else _file.Set(key, value, quoted: true);
        }
        void Flag(string key, bool value) => _file.Set(key, value ? "true" : "false", quoted: false);
        Text("active_profile", ActiveProfile);
        Text("active_version", ActiveVersion);
        Text("previous_version", PreviousVersion);
        Flag("skip_intro", SkipIntro);
        Flag("show_fps", ShowFps);
        Flag("controller_vibration", ControllerVibration);
        Flag("audio_pop_fix", AudioPopFix);
        Flag("async_readbacks", AsyncReadbacks);
        Flag("present_via_dxgi", PresentViaDxgi);
        Flag("compat", Compat);
        Flag("diagnostic_30fps", Diagnostic30Fps);
        Flag("windowed", Windowed);
        Text("window_size", WindowSizes.Label(WindowSize));
        _file.Set("build_jobs", BuildJobs.ToString(System.Globalization.CultureInfo.InvariantCulture), quoted: false);
        _file.Set("vibration_strength", VibrationStrength.ToString(System.Globalization.CultureInfo.InvariantCulture), quoted: false);
        Text("accepted_microsoft_licence", AcceptedMicrosoftLicence);
        _file.Set("toolchain_source", UseVisualStudioInstaller ? "VisualStudio" : "Feed", quoted: true);
        Text("language", Language);
        Text("command_line_options", CommandLineOptions.Length > 0 ? CommandLineOptions : null);
        Text("saves_folder", SavesFolder);
        _file.Save(_path);
    }
}
