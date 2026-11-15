// Copyright (c) 2026, i2kiselev
// BSD 3-Clause License: see LICENSE in the repository root.
using Fm4StaticRecomp.Launcher.Core.Install;
using Fm4StaticRecomp.Launcher.Core.Launch;
using Fm4StaticRecomp.Launcher.Core.Platform;
using Fm4StaticRecomp.Launcher.Core.Profiles;
using Fm4StaticRecomp.Launcher.Core.Settings;

namespace Fm4StaticRecomp.Launcher.Tests.Launch;

public class LaunchArgumentsTests
{
    private static (InstallLayout, Profile, LauncherSettings, TempDir) Setup()
    {
        var temp = new TempDir();
        var layout = new InstallLayout(temp.Path);
        var profile = new Profile("main", Path.Combine(layout.Profiles, "main"), "Main", "Racer", null);
        return (layout, profile, LauncherSettings.Load(layout.LauncherToml), temp);
    }

    [Theory]
    [InlineData("0.3.1", false)]
    [InlineData("0.3.2", true)]
    public void Custom_music_folders_are_forwarded_only_to_supported_versions(string version, bool supported)
    {
        var (layout, profile, settings, temp) = Setup();
        using var _ = temp;
        Directory.CreateDirectory(Path.GetDirectoryName(layout.LauncherToml)!);
        File.WriteAllText(layout.LauncherToml, "menu_music_folder = \"C:/Music/My menu\"\n"
            + "race_music_folder = \"D:/Музыка/Races\"\n");
        settings = LauncherSettings.Load(layout.LauncherToml);
        var args = LaunchArguments.ForPlay(layout, version, profile, settings, new WindowsPlatform());
        Assert.Equal(supported, args.Contains("--fm4_menu_music_folder=C:/Music/My menu"));
        Assert.Equal(supported, args.Contains("--fm4_race_music_folder=D:/Музыка/Races"));
    }

    [Fact]
    public void An_explicit_empty_music_folder_restores_original_music()
    {
        var (layout, profile, settings, temp) = Setup();
        using var _ = temp;
        Directory.CreateDirectory(Path.GetDirectoryName(layout.LauncherToml)!);
        File.WriteAllText(layout.LauncherToml, "menu_music_folder = \"C:/Music\"\n");
        settings = LauncherSettings.Load(layout.LauncherToml);
        settings.CommandLineOptions = "--fm4_menu_music_folder=\"\"";
        var args = LaunchArguments.ForPlay(layout, "0.3.2", profile, settings, new WindowsPlatform());
        Assert.Equal(new[] { "--fm4_menu_music_folder=" }, args.Where(a => a.StartsWith("--fm4_menu_music_folder")).ToArray());
        Assert.DoesNotContain(args, a => a.StartsWith("--fm4_race_music_folder"));
    }

    [Fact]
    public void Play_is_fullscreen_by_default_with_the_size_alt_enter_leaves_to()
    {
        var (layout, profile, settings, temp) = Setup();
        using var _ = temp;
        var args = LaunchArguments.ForPlay(layout, "0.3.1", profile, settings, new WindowsPlatform());
        Assert.DoesNotContain(args, a => a.StartsWith("--fullscreen"));
        Assert.Contains("--window_width=1280", args);
        Assert.Contains("--window_height=720", args);
    }

    [Fact]
    public void Windowed_play_opens_a_window_of_the_chosen_size()
    {
        var (layout, profile, settings, temp) = Setup();
        using var _ = temp;
        settings.Windowed = true;
        settings.WindowSize = (1600, 900);
        var args = LaunchArguments.ForPlay(layout, "0.3.1", profile, settings, new WindowsPlatform());
        Assert.Contains("--fullscreen=false", args);
        Assert.Contains("--window_width=1600", args);
        Assert.Contains("--window_height=900", args);
    }

    [Theory]
    [InlineData("0.3.0")]
    [InlineData("0.2.2")]
    public void A_fullscreen_older_version_gets_no_window_size(string version)
    {
        var (layout, profile, settings, temp) = Setup();
        using var _ = temp;
        var args = LaunchArguments.ForPlay(layout, version, profile, settings, new WindowsPlatform(), displayDpi: 144);
        Assert.DoesNotContain(args, a => a.StartsWith("--window_"));
    }

    [Theory]
    [InlineData("0.3.0")]
    [InlineData("0.2.2")]
    public void A_windowed_older_version_starts_fullscreen(string version)
    {
        var (layout, profile, settings, temp) = Setup();
        using var _ = temp;
        settings.Windowed = true;
        var args = LaunchArguments.ForPlay(layout, version, profile, settings, new WindowsPlatform(), displayDpi: 144);
        Assert.DoesNotContain(args, a => a.StartsWith("--fullscreen"));
        Assert.DoesNotContain(args, a => a.StartsWith("--window_"));
    }

    // The game multiplies its window size by the display's scale, rounding up, and SDL then holds the window at 16:9.
    private static (int Width, int Height) RealWindow(int logicalWidth, int logicalHeight, int dpi)
    {
        var (width, height) = ((logicalWidth * dpi + 95) / 96, (logicalHeight * dpi + 95) / 96);
        var aspect = 16f / 9f;
        if (width / (float)height > aspect) width = (int)MathF.Round(height * aspect, MidpointRounding.AwayFromZero);
        else if (width / (float)height < aspect) height = (int)MathF.Round(width / aspect, MidpointRounding.AwayFromZero);
        return (width, height);
    }

    // One pixel short and a render of the same size no longer fits: its upscaler stays off.
    [Theory]
    [InlineData(144, 2560, 1440)]
    [InlineData(144, 1600, 900)]
    [InlineData(216, 1280, 720)]
    [InlineData(288, 1280, 720)]
    [InlineData(216, 3840, 2160)]
    [InlineData(168, 2560, 1440)]
    public void A_window_on_a_scaled_display_is_never_short_of_its_size(int dpi, int width, int height)
    {
        var (layout, profile, settings, temp) = Setup();
        using var _ = temp;
        settings.WindowSize = (width, height);
        var args = LaunchArguments.ForPlay(layout, "0.3.1", profile, settings, new WindowsPlatform(), displayDpi: dpi);
        var logical = (Width: Value(args, "--window_width="), Height: Value(args, "--window_height="));
        var real = RealWindow(logical.Width, logical.Height, dpi);
        Assert.InRange(real.Width, width, width + 4);
        Assert.InRange(real.Height, height, height + 4);
    }

    private static int Value(IReadOnlyList<string> args, string prefix) =>
        int.Parse(args.Single(a => a.StartsWith(prefix))[prefix.Length..]);

    [Theory]
    [InlineData(120, 1600, 900, 1280, 720)]
    [InlineData(144, 1280, 720, 853, 480)]
    [InlineData(144, 1600, 900, 1067, 600)]
    [InlineData(168, 1600, 900, 914, 514)]
    public void The_window_size_is_in_real_pixels_on_a_scaled_display(int dpi, int width, int height, int logicalWidth, int logicalHeight)
    {
        var (layout, profile, settings, temp) = Setup();
        using var _ = temp;
        settings.WindowSize = (width, height);
        var args = LaunchArguments.ForPlay(layout, "0.3.1", profile, settings, new WindowsPlatform(), displayDpi: dpi);
        Assert.Contains($"--window_width={logicalWidth}", args);
        Assert.Contains($"--window_height={logicalHeight}", args);
    }

    [Fact]
    public void Command_line_options_replace_the_window_settings()
    {
        var (layout, profile, settings, temp) = Setup();
        using var _ = temp;
        settings.Windowed = true;
        settings.CommandLineOptions = "--fullscreen=true --window_width=1000 --window_height=500";
        var args = LaunchArguments.ForPlay(layout, "0.1.0", profile, settings, new WindowsPlatform());
        Assert.Equal(["--fullscreen=true"], args.Where(a => a.StartsWith("--fullscreen")));
        Assert.Equal(["--window_width=1000"], args.Where(a => a.StartsWith("--window_width")));
        Assert.Equal(["--window_height=500"], args.Where(a => a.StartsWith("--window_height")));
    }

    [Fact]
    public void Play_points_the_game_at_the_profile_and_shared_folders()
    {
        var (layout, profile, settings, temp) = Setup();
        using var _ = temp;
        var args = LaunchArguments.ForPlay(layout, "0.1.0", profile, settings, new WindowsPlatform());
        Assert.Contains($"--game_data_root={layout.Game}", args);
        Assert.Contains($"--user_data_root={profile.User}", args);
        Assert.Contains($"--cache_root={layout.Cache("0.1.0")}", args);
        Assert.Contains($"--fm4_config_root={layout.Config}", args);
        Assert.Contains("--user_name=Racer", args);
        Assert.Contains($"--update_data_root={layout.Update}", args);
        Assert.Contains($"--log_file={layout.RuntimeLog}", args);
        Assert.Contains("--fm4_skip_intro=true", args);
        Assert.Contains("--fm4_language=en-US", args);
        Assert.DoesNotContain(args, a => a.StartsWith("--fh1_async_readbacks"));
        Assert.DoesNotContain(args, a => a.StartsWith("--user_xuid"));
    }

    [Fact]
    public void Play_keeps_the_console_substeps_per_tick_only_on_a_project_forza_plus_install()
    {
        var (layout, profile, settings, temp) = Setup();
        using var _ = temp;
        Assert.DoesNotContain(LaunchArguments.ForPlay(layout, "0.1.0", profile with { Game = "pfp4" }, settings, new WindowsPlatform(), "vanilla"),
            a => a.StartsWith("--fm4_car_min_substeps"));
        Assert.Contains("--fm4_car_min_substeps=6",
            LaunchArguments.ForPlay(layout, "0.1.0", profile, settings, new WindowsPlatform(), "pfp4"));
    }

    [Fact]
    public void The_30_fps_diagnostic_sets_the_frame_rate_only_when_on()
    {
        var (layout, profile, settings, temp) = Setup();
        using var _ = temp;
        Assert.DoesNotContain(LaunchArguments.ForPlay(layout, "0.3.1", profile, settings, new WindowsPlatform()),
            a => a.StartsWith("--fm4_frame_rate"));
        settings.Diagnostic30Fps = true;
        Assert.Contains("--fm4_frame_rate=30", LaunchArguments.ForPlay(layout, "0.3.1", profile, settings, new WindowsPlatform()));
    }

    [Fact]
    public void A_command_line_frame_rate_replaces_the_30_fps_diagnostic()
    {
        var (layout, profile, settings, temp) = Setup();
        using var _ = temp;
        settings.Diagnostic30Fps = true;
        settings.CommandLineOptions = "--fm4_frame_rate=45";
        var args = LaunchArguments.ForPlay(layout, "0.3.1", profile, settings, new WindowsPlatform());
        Assert.Equal(["--fm4_frame_rate=45"], args.Where(a => a.StartsWith("--fm4_frame_rate")));
    }

    [Fact]
    public void The_games_own_vibration_strength_passes_no_option()
    {
        var (layout, profile, settings, temp) = Setup();
        using var _ = temp;
        Assert.DoesNotContain(LaunchArguments.ForPlay(layout, "0.3.2", profile, settings, new WindowsPlatform()),
            a => a.StartsWith("--pad_rumble_strength"));
    }

    [Theory]
    [InlineData(0)]
    [InlineData(55)]
    [InlineData(200)]
    public void A_chosen_vibration_strength_sets_the_rumble_strength(int strength)
    {
        var (layout, profile, settings, temp) = Setup();
        using var _ = temp;
        settings.VibrationStrength = strength;
        Assert.Contains($"--pad_rumble_strength={strength}",
            LaunchArguments.ForPlay(layout, "0.3.2", profile, settings, new WindowsPlatform()));
    }

    // Before 0.3.2 the strength only went down to 100; an older version (GO BACK TO) plays as it shipped.
    [Theory]
    [InlineData("0.3.1")]
    [InlineData("0.3.0")]
    public void An_older_version_keeps_its_default_vibration(string version)
    {
        var (layout, profile, settings, temp) = Setup();
        using var _ = temp;
        settings.VibrationStrength = 40;
        Assert.DoesNotContain(LaunchArguments.ForPlay(layout, version, profile, settings, new WindowsPlatform()),
            a => a.StartsWith("--pad_rumble_strength"));
    }

    [Theory]
    [InlineData(false, "0.3.2", null)]
    [InlineData(true, "0.3.1", null)]
    [InlineData(true, "0.3.2", "--fm4_audio_compatibility=true")]
    [InlineData(true, "0.4.0", "--fm4_audio_compatibility=true")]
    public void Audio_compatibility_requires_opt_in_and_a_supported_version(bool enabled, string version, string? expected)
    {
        var (layout, profile, settings, temp) = Setup();
        using var _ = temp;
        settings.AudioCompatibility = enabled;
        var options = LaunchArguments.ForPlay(layout, version, profile, settings, new WindowsPlatform())
            .Where(a => a.StartsWith("--fm4_audio_compatibility")).ToArray();
        if (expected is null) Assert.Empty(options);
        else Assert.Equal(new[] { expected }, options);
    }

    [Theory]
    [InlineData(false, "0.3.3", null)]
    [InlineData(true, "0.3.2", null)]
    [InlineData(true, "0.3.3", "--fm4_keep_music_on_crash=true")]
    [InlineData(true, "0.4.0", "--fm4_keep_music_on_crash=true")]
    public void Music_on_crash_requires_opt_in_and_a_supported_version(bool keep, string version, string? expected)
    {
        var (layout, profile, settings, temp) = Setup();
        using var _ = temp;
        settings.KeepMusicOnCrash = keep;
        var options = LaunchArguments.ForPlay(layout, version, profile, settings, new WindowsPlatform())
            .Where(a => a.StartsWith("--fm4_keep_music_on_crash")).ToArray();
        if (expected is null) Assert.Empty(options);
        else Assert.Equal(new[] { expected }, options);
    }

    [Theory]
    [InlineData(false, "0.3.3", null)]
    [InlineData(true, "0.3.2", null)]
    [InlineData(true, "0.3.3", "--fm4_music_filter=light")]
    [InlineData(true, "0.4.0", "--fm4_music_filter=light")]
    public void Light_music_filter_requires_opt_in_and_a_supported_version(bool light, string version, string? expected)
    {
        var (layout, profile, settings, temp) = Setup();
        using var _ = temp;
        settings.LightMusicFilter = light;
        var options = LaunchArguments.ForPlay(layout, version, profile, settings, new WindowsPlatform())
            .Where(a => a.StartsWith("--fm4_music_filter")).ToArray();
        if (expected is null) Assert.Empty(options);
        else Assert.Equal(new[] { expected }, options);
    }

    [Fact]
    public void An_explicit_audio_compatibility_override_wins()
    {
        var (layout, profile, settings, temp) = Setup();
        using var _ = temp;
        settings.AudioCompatibility = true;
        settings.CommandLineOptions = "--fm4_audio_compatibility=false";
        var options = LaunchArguments.ForPlay(layout, "0.3.2", profile, settings, new WindowsPlatform())
            .Where(a => a.StartsWith("--fm4_audio_compatibility")).ToArray();
        Assert.Equal(new[] { "--fm4_audio_compatibility=false" }, options);
    }

    [Fact]
    public void The_audio_pop_fix_on_by_default_passes_no_option()
    {
        var (layout, profile, settings, temp) = Setup();
        using var _ = temp;
        Assert.DoesNotContain(LaunchArguments.ForPlay(layout, "0.3.2", profile, settings, new WindowsPlatform()),
            a => a.StartsWith("--fm4_audio_pop_fix"));
    }

    [Fact]
    public void The_audio_pop_fix_turned_off_passes_it_off()
    {
        var (layout, profile, settings, temp) = Setup();
        using var _ = temp;
        settings.AudioPopFix = false;
        Assert.Contains("--fm4_audio_pop_fix=false",
            LaunchArguments.ForPlay(layout, "0.3.2", profile, settings, new WindowsPlatform()));
    }

    // An older version has no AUDIO POP FIX and plays the audio as it shipped.
    [Fact]
    public void An_older_version_gets_no_audio_pop_fix_option()
    {
        var (layout, profile, settings, temp) = Setup();
        using var _ = temp;
        settings.AudioPopFix = false;
        Assert.DoesNotContain(LaunchArguments.ForPlay(layout, "0.3.1", profile, settings, new WindowsPlatform()),
            a => a.StartsWith("--fm4_audio_pop_fix"));
    }

    [Fact]
    public void Play_signs_in_with_the_xuid_of_an_imported_xenia_save()
    {
        var (layout, profile, settings, temp) = Setup();
        using var _ = temp;
        var args = LaunchArguments.ForPlay(layout, "0.1.0", profile with { Xuid = "E030000047B76145" }, settings, new WindowsPlatform());
        Assert.Contains("--user_xuid=E030000047B76145", args);
    }

    [Fact]
    public void Play_starts_the_game_in_the_chosen_language()
    {
        var (layout, profile, settings, temp) = Setup();
        using var _ = temp;
        settings.Language = "pt-BR";
        var args = LaunchArguments.ForPlay(layout, "0.1.0", profile, settings, new WindowsPlatform());
        Assert.Contains("--fm4_language=pt-BR", args);
    }

    [Fact]
    public void Skip_intro_off_leaves_the_option_out()
    {
        var (layout, profile, settings, temp) = Setup();
        using var _ = temp;
        settings.SkipIntro = false;
        var args = LaunchArguments.ForPlay(layout, "0.1.0", profile, settings, new WindowsPlatform());
        Assert.DoesNotContain(args, a => a.StartsWith("--fm4_skip_intro"));
    }

    [Theory]
    [InlineData("0.3.2")]
    [InlineData("0.3.3")]
    public void Show_fps_passes_the_flag_when_supported(string version)
    {
        var (layout, profile, settings, temp) = Setup();
        using var _ = temp;
        settings.ShowFps = true;
        var args = LaunchArguments.ForPlay(layout, version, profile, settings, new WindowsPlatform());
        Assert.Equal(["--fm4_show_fps=true"], args.Where(a => a.StartsWith("--fm4_show_fps=")));
    }

    [Theory]
    [InlineData("0.3.0")]
    [InlineData("0.3.1")]
    public void An_older_version_gets_no_fps_or_vibration_options(string version)
    {
        var (layout, profile, settings, temp) = Setup();
        using var _ = temp;
        settings.ShowFps = true;
        settings.ControllerVibration = false;
        settings.VibrationStrength = 55;
        var args = LaunchArguments.ForPlay(layout, version, profile, settings, new WindowsPlatform());
        Assert.DoesNotContain(args, a => a.StartsWith("--fm4_show_fps") ||
            a.StartsWith("--controller_vibration") || a.StartsWith("--pad_rumble_strength"));
    }

    [Fact]
    public void Reenabling_controller_vibration_restores_the_saved_strength()
    {
        var (layout, profile, settings, temp) = Setup();
        using var _ = temp;
        settings.ControllerVibration = false;
        settings.VibrationStrength = 55;
        Assert.Contains("--pad_rumble_strength=0",
            LaunchArguments.ForPlay(layout, "0.3.2", profile, settings, new WindowsPlatform()));
        settings.ControllerVibration = true;
        var args = LaunchArguments.ForPlay(layout, "0.3.2", profile, settings, new WindowsPlatform());
        Assert.Equal(["--pad_rumble_strength=55"], args.Where(a => a.StartsWith("--pad_rumble_strength=")));
        Assert.DoesNotContain(args, a => a.StartsWith("--controller_vibration"));
        Assert.Equal(55, settings.VibrationStrength);
    }

    [Fact]
    public void Explicit_fps_and_vibration_options_replace_launcher_values()
    {
        var (layout, profile, settings, temp) = Setup();
        using var _ = temp;
        settings.ShowFps = true;
        settings.ControllerVibration = false;
        settings.CommandLineOptions = "--fm4_show_fps=false --controller_vibration=true --pad_rumble_strength=55";
        var args = LaunchArguments.ForPlay(layout, "0.3.2", profile, settings, new WindowsPlatform());
        Assert.Equal(["--fm4_show_fps=false"], args.Where(a => a.StartsWith("--fm4_show_fps=")));
        Assert.Equal(["--controller_vibration=true"], args.Where(a => a.StartsWith("--controller_vibration=")));
        Assert.Equal(["--pad_rumble_strength=55"], args.Where(a => a.StartsWith("--pad_rumble_strength=")));
    }

    [Theory]
    [InlineData(0)]
    [InlineData(55)]
    [InlineData(100)]
    [InlineData(200)]
    public void Controller_vibration_off_overrides_every_strength(int strength)
    {
        var (layout, profile, settings, temp) = Setup();
        using var _ = temp;
        settings.ControllerVibration = false;
        settings.VibrationStrength = strength;
        var args = LaunchArguments.ForPlay(layout, "0.3.2", profile, settings, new WindowsPlatform());
        Assert.Contains("--controller_vibration=false", args);
        Assert.Equal(["--pad_rumble_strength=0"], args.Where(a => a.StartsWith("--pad_rumble_strength=")));
    }

    [Fact]
    public void Never_repeats_an_option()
    {
        var (layout, profile, settings, temp) = Setup();
        using var _ = temp;
        settings.Compat = true;
        settings.AsyncReadbacks = false;
        settings.PresentViaDxgi = true;
        var args = LaunchArguments.ForPlay(layout, "0.1.0", profile, settings, new WindowsPlatform());
        var names = args.Select(a => a.Split('=')[0]).ToList();
        Assert.Equal(names.Count, names.Distinct().Count());
        Assert.Contains("--fh1_async_readbacks=false", args);
        Assert.Contains("--vulkan_present_via_dxgi=true", args);
    }

    [Fact]
    public void Command_line_options_replace_the_launchers_own_values()
    {
        var (layout, profile, settings, temp) = Setup();
        using var _ = temp;
        settings.Compat = true;
        settings.CommandLineOptions = "--fm4_language=ru --fm4_shadow_map_size=2048  --present_upscaler_view=motion --fm4_language=de";
        var args = LaunchArguments.ForPlay(layout, "0.1.0", profile, settings, new WindowsPlatform());
        var names = args.Select(a => a.Split('=')[0]).ToList();
        Assert.Equal(names.Count, names.Distinct().Count());
        Assert.Contains("--fm4_language=de", args);
        Assert.Contains("--fm4_shadow_map_size=2048", args);
        Assert.Equal("--present_upscaler_view=motion", args[^2]);
        Assert.Contains("--fm4_skip_intro=true", args);
    }

    [Fact]
    public void A_negated_command_line_flag_replaces_the_launchers_value()
    {
        var (layout, profile, settings, temp) = Setup();
        using var _ = temp;
        settings.CommandLineOptions = "--no-fm4_skip_intro";
        var args = LaunchArguments.ForPlay(layout, "0.1.0", profile, settings, new WindowsPlatform());
        Assert.DoesNotContain("--fm4_skip_intro=true", args);
        Assert.Equal("--no-fm4_skip_intro", args[^1]);
    }

    [Fact]
    public void A_quoted_command_line_option_keeps_its_spaces()
    {
        var (layout, profile, settings, temp) = Setup();
        using var _ = temp;
        settings.CommandLineOptions = "--fm4_dlc_packages=\"D:\\My DLC\\\" --flag";
        var args = LaunchArguments.ForPlay(layout, "0.1.0", profile, settings, new WindowsPlatform());
        Assert.Equal(["--fm4_dlc_packages=D:\\My DLC\\", "--flag"], args.TakeLast(2));
    }

    [Fact]
    public void Dxgi_is_dropped_where_unsupported()
    {
        var (layout, profile, settings, temp) = Setup();
        using var _ = temp;
        settings.PresentViaDxgi = true;
        var args = LaunchArguments.ForPlay(layout, "0.1.0", profile, settings, new LinuxPlatform());
        Assert.DoesNotContain(args, a => a.StartsWith("--vulkan_present_via_dxgi"));
    }
}
