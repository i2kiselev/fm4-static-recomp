// Copyright (c) 2026, i2kiselev
// BSD 3-Clause License: see LICENSE in the repository root.
using Fm4StaticRecomp.Launcher.Core.Settings;

namespace Fm4StaticRecomp.Launcher.Tests.Settings;

public class LauncherSettingsTests
{
    [Fact]
    public void A_missing_file_gives_the_defaults()
    {
        using var temp = new TempDir();
        var settings = LauncherSettings.Load(Path.Combine(temp.Path, "launcher.toml"));
        Assert.True(settings.SkipIntro);
        Assert.False(settings.ShowFps);
        Assert.True(settings.ControllerVibration);
        Assert.True(settings.AsyncReadbacks);
        Assert.False(settings.PresentViaDxgi);
        Assert.False(settings.Windowed);
        Assert.Equal((1280, 720), settings.WindowSize);
        Assert.False(settings.Compat);
        Assert.False(settings.Diagnostic30Fps);
        Assert.False(settings.UseVisualStudioInstaller);
        Assert.Equal(0, settings.BuildJobs);
        Assert.Equal(100, settings.VibrationStrength);
        Assert.True(settings.AudioPopFix);
        Assert.Null(settings.ActiveProfile);
        Assert.Null(settings.ActiveVersion);
        Assert.Null(settings.PreviousVersion);
        Assert.Null(settings.AcceptedMicrosoftLicence);
        Assert.Equal("en-US", settings.Language);
        Assert.Equal("", settings.CommandLineOptions);
        Assert.Null(settings.SavesFolder);
    }

    [Fact]
    public void The_window_settings_round_trip()
    {
        using var temp = new TempDir();
        var path = Path.Combine(temp.Path, "launcher.toml");
        var settings = LauncherSettings.Load(path);
        settings.Windowed = true;
        settings.WindowSize = (1920, 1080);
        settings.Save();
        var loaded = LauncherSettings.Load(path);
        Assert.True(loaded.Windowed);
        Assert.Equal((1920, 1080), loaded.WindowSize);
    }

    [Theory]
    [InlineData("1000x500")]
    [InlineData("big")]
    public void A_window_size_not_on_the_list_falls_back_to_the_default(string written)
    {
        using var temp = new TempDir();
        var path = Path.Combine(temp.Path, "launcher.toml");
        File.WriteAllText(path, $"window_size = \"{written}\"");
        Assert.Equal((1280, 720), LauncherSettings.Load(path).WindowSize);
    }

    [Fact]
    public void Offers_the_window_sizes_smaller_than_the_display()
    {
        Assert.Equal([(1280, 720), (1600, 900), (1920, 1080)], WindowSizes.Fitting((2560, 1440), (1280, 720)));
        Assert.Equal([(1280, 720)], WindowSizes.Fitting((1366, 768), (1280, 720)));
    }

    [Fact]
    public void Offers_the_smallest_window_size_and_the_chosen_one_whatever_the_display()
    {
        Assert.Equal([(1280, 720)], WindowSizes.Fitting((1280, 720), (1280, 720)));
        Assert.Equal([(1280, 720), (2560, 1440)], WindowSizes.Fitting((1366, 768), (2560, 1440)));
    }

    [Fact]
    public void Offers_every_window_size_on_an_unknown_display() =>
        Assert.Equal(WindowSizes.All, WindowSizes.Fitting(null, (1280, 720)));

    [Fact]
    public void Round_trips_every_property()
    {
        using var temp = new TempDir();
        var path = Path.Combine(temp.Path, "config", "launcher.toml");
        var settings = LauncherSettings.Load(path);
        settings.ActiveProfile = "main";
        settings.ActiveVersion = "0.2.0";
        settings.PreviousVersion = "0.1.3";
        settings.SkipIntro = false;
        settings.ShowFps = true;
        settings.ControllerVibration = false;
        settings.AsyncReadbacks = false;
        settings.PresentViaDxgi = true;
        settings.Compat = true;
        settings.Diagnostic30Fps = true;
        settings.BuildJobs = 6;
        settings.VibrationStrength = 160;
        settings.AudioPopFix = false;
        settings.AcceptedMicrosoftLicence = "https://a https://b";
        settings.UseVisualStudioInstaller = true;
        settings.Language = "ru";
        settings.CommandLineOptions = "--fm4_dlc_packages=\"D:\\My DLC\" --present_upscaler_view=motion";
        settings.SavesFolder = @"D:\FM4 Saves";
        settings.Save();
        var read = LauncherSettings.Load(path);
        Assert.Equal("--fm4_dlc_packages=\"D:\\My DLC\" --present_upscaler_view=motion", read.CommandLineOptions);
        Assert.Equal(("main", "0.2.0", "0.1.3"), (read.ActiveProfile, read.ActiveVersion, read.PreviousVersion));
        Assert.Equal((false, false, true, true, true), (read.SkipIntro, read.AsyncReadbacks, read.PresentViaDxgi, read.Compat,
            read.UseVisualStudioInstaller));
        Assert.True(read.ShowFps);
        Assert.False(read.ControllerVibration);
        Assert.True(read.Diagnostic30Fps);
        Assert.Equal(6, read.BuildJobs);
        Assert.Equal(160, read.VibrationStrength);
        Assert.False(read.AudioPopFix);
        Assert.Equal("https://a https://b", read.AcceptedMicrosoftLicence);
        Assert.Equal("ru", read.Language);
        Assert.Equal(@"D:\FM4 Saves", read.SavesFolder);
        Assert.Contains("toolchain_source = \"VisualStudio\"", File.ReadAllText(path));
        read.UseVisualStudioInstaller = false;
        read.Save();
        Assert.Contains("toolchain_source = \"Feed\"", File.ReadAllText(path));
    }

    [Fact]
    public void Clears_a_text_set_to_null()
    {
        using var temp = new TempDir();
        var path = Path.Combine(temp.Path, "launcher.toml");
        var settings = LauncherSettings.Load(path);
        settings.ActiveProfile = "main";
        settings.PreviousVersion = "0.1.3";
        settings.CommandLineOptions = "--a=1";
        settings.Save();
        settings.ActiveProfile = null;
        settings.PreviousVersion = null;
        settings.CommandLineOptions = "";
        settings.Save();
        var read = LauncherSettings.Load(path);
        Assert.Null(read.ActiveProfile);
        Assert.Null(read.PreviousVersion);
        Assert.DoesNotContain("active_profile", File.ReadAllText(path));
        Assert.DoesNotContain("command_line_options", File.ReadAllText(path));
    }

    [Fact]
    public void A_build_job_count_out_of_range_reads_as_automatic()
    {
        using var temp = new TempDir();
        var settings = LauncherSettings.Load(temp.File("launcher.toml", "build_jobs = 99\n"u8.ToArray()));
        Assert.Equal(0, settings.BuildJobs);
    }

    [Theory]
    [InlineData("vibration_strength = 201\n")]
    [InlineData("vibration_strength = -1\n")]
    [InlineData("vibration_strength = \"strong\"\n")]
    public void A_vibration_strength_out_of_range_reads_as_the_games_own(string written)
    {
        using var temp = new TempDir();
        var settings = LauncherSettings.Load(temp.File("launcher.toml", System.Text.Encoding.UTF8.GetBytes(written)));
        Assert.Equal(100, settings.VibrationStrength);
    }

    [Fact]
    public void A_language_the_game_does_not_offer_reads_as_english()
    {
        using var temp = new TempDir();
        var settings = LauncherSettings.Load(temp.File("launcher.toml", "language = \"sv\"\n"u8.ToArray()));
        Assert.Equal("en-US", settings.Language);
    }

    [Fact]
    public void Keeps_keys_it_does_not_know()
    {
        using var temp = new TempDir();
        var path = temp.File("launcher.toml", "future_key = 3\n"u8.ToArray());
        var settings = LauncherSettings.Load(path);
        settings.Compat = true;
        settings.Save();
        Assert.StartsWith("future_key = 3\n", File.ReadAllText(path));
    }
}
