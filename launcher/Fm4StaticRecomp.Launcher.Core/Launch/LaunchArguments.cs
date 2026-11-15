// Copyright (c) 2026, i2kiselev
// BSD 3-Clause License: see LICENSE in the repository root.
using System.Globalization;
using Fm4StaticRecomp.Launcher.Core.Install;
using Fm4StaticRecomp.Launcher.Core.Platform;
using Fm4StaticRecomp.Launcher.Core.Profiles;
using Fm4StaticRecomp.Launcher.Core.Settings;

namespace Fm4StaticRecomp.Launcher.Core.Launch;

public static class LaunchArguments
{
    // The list of tools\run.ps1 -Compat, its source of truth: every rendering shortcut that has a switch off.
    private static readonly (string Name, string Value)[] CompatOptions =
    [
        ("fh1_async_readbacks", "false"), ("fh1_readback_all_resolves", "true"),
        ("fh1_surfaceless_clears", "false"), ("fh1_color_overwrite_claims", "false"),
        ("fh1_scrambled_stencil_claims", "false"), ("fh1_scaled_msaa_single_sample", "false"),
        ("gpu_record_thread", "false"), ("clear_memory_page_state", "true"),
        ("texture_cache_memory_limit_soft", "1024"), ("anisotropic_override", "-1"),
        ("fm4_level_of_detail", "console"), ("fm4_shadow_map_size", "512"),
        ("fm4_ground_shadow_size", "256"),
        ("fm4_reflection_cube_size", "256"), ("fm4_full_rate_scenes", "false"),
    ];

    public static IReadOnlyList<string> ForPlay(InstallLayout layout, string version, Profile profile,
        LauncherSettings settings, IPlatform platform, string installGame = "vanilla", int displayDpi = 96)
    {
        var options = new Dictionary<string, string>
        {
            ["game_data_root"] = layout.Game,
            ["user_data_root"] = profile.User,
            ["update_data_root"] = layout.Update,
            ["cache_root"] = layout.Cache(version),
            ["log_file"] = layout.RuntimeLog,
            ["fm4_config_root"] = layout.Config,
            ["user_name"] = profile.Gamertag,
            ["fm4_language"] = settings.Language,
        };
        if (OperatingSystem.IsMacOS()) options["gpu_backend"] = "vulkan";
        if (profile.Xuid is { } xuid) options["user_xuid"] = xuid;
        // The mod's AI is tuned on Xenia, about six physics substeps a tick at its 62 fps.
        if (installGame == ProfileStore.Pfp4) options["fm4_car_min_substeps"] = "6";
        if (settings.SkipIntro) options["fm4_skip_intro"] = "true";
        if (settings.ShowFps && !Predates(version, new Version(0, 3, 2))) options["fm4_show_fps"] = "true";
        if (!settings.ControllerVibration && !Predates(version, new Version(0, 3, 2)))
        {
            options["controller_vibration"] = "false";
            options["pad_rumble_strength"] = "0";
        }
        if (settings.Compat) foreach (var (name, value) in CompatOptions) options[name] = value;
        if (!settings.AsyncReadbacks) options["fh1_async_readbacks"] = "false";
        if (settings.PresentViaDxgi && platform.SupportsDxgiPresentation) options["vulkan_present_via_dxgi"] = "true";
        if (settings.Diagnostic30Fps) options["fm4_frame_rate"] = "30";
        if (settings.ControllerVibration && settings.VibrationStrength != LauncherSettings.GameVibrationStrength
            && !Predates(version, new Version(0, 3, 2)))
            options["pad_rumble_strength"] = settings.VibrationStrength.ToString(CultureInfo.InvariantCulture);
        if (settings.AudioCompatibility && !Predates(version, new Version(0, 3, 2))) options["fm4_audio_compatibility"] = "true";
        if (!settings.AudioPopFix && !Predates(version, new Version(0, 3, 2))) options["fm4_audio_pop_fix"] = "false";
        if (settings.KeepMusicOnCrash && !Predates(version, new Version(0, 3, 3))) options["fm4_keep_music_on_crash"] = "true";
        if (settings.LightMusicFilter && !Predates(version, new Version(0, 3, 3))) options["fm4_music_filter"] = "light";
        if (!Predates(version, new Version(0, 3, 2)))
        {
            if (!string.IsNullOrWhiteSpace(settings.MenuMusicFolder)) options["fm4_menu_music_folder"] = settings.MenuMusicFolder;
            if (!string.IsNullOrWhiteSpace(settings.RaceMusicFolder)) options["fm4_race_music_folder"] = settings.RaceMusicFolder;
        }
        // In fullscreen too: Alt+Enter leaves to this size. An older version (GO BACK TO, or while an update
        // builds) starts fullscreen: it takes the window's size for the guest's video mode, and below 1280x720,
        // on a scaled display, the game changes its text layout.
        if (!Predates(version, new Version(0, 3, 1)))
        {
            if (settings.Windowed) options["fullscreen"] = "false";
            options["window_width"] = LogicalSize(settings.WindowSize.Width, displayDpi).ToString(CultureInfo.InvariantCulture);
            options["window_height"] = LogicalSize(settings.WindowSize.Height, displayDpi).ToString(CultureInfo.InvariantCulture);
        }
        var args = options.Select(o => $"--{o.Key}={o.Value}").ToList();
        // A COMMAND-LINE OPTIONS entry replaces the launcher's value, never repeats it: the SDK drops every
        // argument when one is given twice.
        foreach (var option in SplitCommandLine(settings.CommandLineOptions))
        {
            var name = NameOf(option);
            args.RemoveAll(a => NameOf(a) == name);
            args.Add(option);
        }
        return args;
    }

    // The smallest size the game's display scale, rounding up, takes to at least the real one: one pixel
    // short, a render of that size no longer fits and its upscaler stays off.
    private static int LogicalSize(int real, int dpi)
    {
        var logical = real * 96 / dpi;
        return (logical * dpi + 95) / 96 < real ? logical + 1 : logical;
    }

    private static bool Predates(string version, Version first) =>
        Version.TryParse(version, out var parsed) && parsed < first;

    // The SDK reads --no-name as --name=false, the same option.
    private static string NameOf(string argument) =>
        argument.Split('=', 2)[0] is var name && name.StartsWith("--no-", StringComparison.Ordinal) ? "--" + name[5..] : name;

    // Splits at spaces outside double quotes and drops the quotes, so --opt="C:\a b" is one argument.
    // A backslash is a path separator here, not an escape.
    private static IReadOnlyList<string> SplitCommandLine(string text)
    {
        var args = new List<string>();
        var current = new System.Text.StringBuilder();
        var quoted = false;
        var started = false;
        foreach (var c in text)
        {
            if (c == '"') (quoted, started) = (!quoted, true);
            else if (char.IsWhiteSpace(c) && !quoted)
            {
                if (started) args.Add(current.ToString());
                (current, started) = (new(), false);
            }
            else (current, started) = (current.Append(c), true);
        }
        if (started) args.Add(current.ToString());
        return args;
    }

    public static IReadOnlyList<string> ForContentInstall(InstallLayout layout, string? disc2Folder,
        string? dlcFolder, string logFile)
    {
        var args = new List<string>
        {
            $"--game_data_root={layout.Game}",
            $"--user_data_root={layout.ContentInstallUser}",
            $"--log_file={logFile}",
            "--fm4_install_only=true",
        };
        if (disc2Folder is not null) args.Add($"--fm4_disc2_packages={disc2Folder}");
        if (dlcFolder is not null) args.Add($"--fm4_dlc_packages={dlcFolder}");
        return args;
    }
}
