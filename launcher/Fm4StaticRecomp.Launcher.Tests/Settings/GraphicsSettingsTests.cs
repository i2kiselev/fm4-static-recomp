using Fm4StaticRecomp.Launcher.Core.Settings;

namespace Fm4StaticRecomp.Launcher.Tests.Settings;

public class GraphicsSettingsTests
{
    private static GraphicsSchema Schema() =>
        GraphicsSchema.Load(Path.Combine(AppContext.BaseDirectory, "Settings", "schema-sample.json"));

    [Fact]
    public void Uses_defaults_for_missing_or_invalid_values()
    {
        using var temp = new TempDir();
        var toml = temp.File("graphics.toml", "resolution_scale = 9\nfm4_frame_rate = 150\n"u8.ToArray());
        var settings = new GraphicsSettings(Schema(), toml, Path.Combine(temp.Path, "backups"));
        Assert.Equal("1", settings.Value("resolution_scale"));
        Assert.Equal("150", settings.Value("fm4_frame_rate"));
        Assert.Equal("none", settings.Value("swap_post_effect"));
    }

    [Fact]
    public void Matches_a_preset_or_reports_custom()
    {
        using var temp = new TempDir();
        var settings = new GraphicsSettings(Schema(), Path.Combine(temp.Path, "g.toml"), Path.Combine(temp.Path, "b"));
        Assert.Null(settings.CurrentPreset());
        settings.ApplyPreset("HIGH");
        Assert.Equal("HIGH", settings.CurrentPreset());
        settings.Set("swap_post_effect", "none");
        Assert.Null(settings.CurrentPreset());
    }

    [Fact]
    public void Save_keeps_other_keys_and_changes_the_game_made_meanwhile()
    {
        using var temp = new TempDir();
        var toml = temp.File("graphics.toml", "fm4_frame_rate = 120\nunknown = 1\n"u8.ToArray());
        var settings = new GraphicsSettings(Schema(), toml, Path.Combine(temp.Path, "backups"));
        File.WriteAllText(toml, "fm4_frame_rate = 144\nunknown = 1\nresolution_scale = 2\n");
        settings.Set("swap_post_effect", "fxaa");
        settings.Save();
        Assert.Equal("fm4_frame_rate = 144\nunknown = 1\nresolution_scale = 2\nswap_post_effect = \"fxaa\"\n",
            File.ReadAllText(toml));
        Assert.Single(Directory.GetFiles(Path.Combine(temp.Path, "backups")));
    }

    [Fact]
    public void Frame_rate_ceiling_follows_the_display()
    {
        using var temp = new TempDir();
        var settings = new GraphicsSettings(Schema(), Path.Combine(temp.Path, "g.toml"), temp.Path);
        Assert.Equal(170, settings.FrameRateCeiling(170));
        Assert.Equal(240, settings.FrameRateCeiling(null));
        Assert.Equal(240, settings.FrameRateCeiling(360));
    }

    [Fact]
    public void A_malformed_file_shows_defaults_and_is_backed_up_before_saving()
    {
        using var temp = new TempDir();
        var original = "garbage\nresolution_scale = 2\n"u8.ToArray();
        var toml = temp.File("graphics.toml", original);
        var backups = Path.Combine(temp.Path, "backups");
        var settings = new GraphicsSettings(Schema(), toml, backups);
        Assert.True(settings.FileIsMalformed);
        Assert.Equal("2", settings.Value("resolution_scale"));
        Assert.Equal("none", settings.Value("swap_post_effect"));
        settings.Set("swap_post_effect", "fxaa");
        settings.Save();
        Assert.Equal(original, File.ReadAllBytes(Assert.Single(Directory.GetFiles(backups))));
        Assert.StartsWith("garbage\n", File.ReadAllText(toml));
    }

    [Fact]
    public void Restores_the_newest_backup()
    {
        using var temp = new TempDir();
        var toml = temp.File("graphics.toml", "resolution_scale = 1\n"u8.ToArray());
        var backups = Path.Combine(temp.Path, "backups");
        var first = new GraphicsSettings(Schema(), toml, backups);
        first.Set("resolution_scale", "2");
        first.Save();
        var second = new GraphicsSettings(Schema(), toml, backups);
        second.Set("swap_post_effect", "fxaa");
        second.Save();
        Assert.Equal(2, Directory.GetFiles(backups).Length);
        File.WriteAllText(toml, "resolution_scale = 1\nswap_post_effect = \"fxaa\"\n");
        var settings = new GraphicsSettings(Schema(), toml, backups);
        settings.Set("fm4_frame_rate", "144");
        Assert.True(settings.RestoreBackup());
        Assert.Equal("resolution_scale = 2\n", File.ReadAllText(toml));
        Assert.Equal("2", settings.Value("resolution_scale"));
        Assert.Equal("120", settings.Value("fm4_frame_rate"));
        settings.Save();
        Assert.Equal("resolution_scale = 2\n", File.ReadAllText(toml));
    }

    [Fact]
    public void Restoring_without_a_backup_changes_nothing()
    {
        using var temp = new TempDir();
        var toml = temp.File("graphics.toml", "resolution_scale = 2\n"u8.ToArray());
        Directory.CreateDirectory(Path.Combine(temp.Path, "backups"));
        var settings = new GraphicsSettings(Schema(), toml, Path.Combine(temp.Path, "backups"));
        Assert.False(settings.RestoreBackup());
        Assert.Equal("resolution_scale = 2\n", File.ReadAllText(toml));
    }

    [Fact]
    public void Reset_stages_every_default()
    {
        using var temp = new TempDir();
        var toml = temp.File("graphics.toml", "resolution_scale = 2\nswap_post_effect = \"fxaa\"\n"u8.ToArray());
        var settings = new GraphicsSettings(Schema(), toml, Path.Combine(temp.Path, "backups"));
        settings.ResetToDefaults();
        Assert.Equal("1", settings.Value("resolution_scale"));
        settings.Save();
        Assert.Equal("resolution_scale = 1\nswap_post_effect = \"none\"\nfm4_frame_rate = 120\nfh1_scaled_msaa_single_sample = true\n",
            File.ReadAllText(toml));
    }

    [Fact]
    public void Shows_a_row_only_while_the_setting_it_depends_on_has_its_value()
    {
        using var temp = new TempDir();
        var json = temp.File("schema.json", """
            {"schema_version":1,"blocks":[{"rows":[
             {"label":"UPSCALER","key":"present_effect","string":true,"kind":"choice","choices":[{"label":"OFF","value":"bilinear"},{"label":"FSR 3","value":"fsr3"},{"label":"DLSS","value":"dlss"}],"default":"bilinear","description":"d"},
             {"label":"DLSS PRESET","key":"present_dlss_preset","string":true,"kind":"choice","choices":[{"label":"AUTO","value":"auto"},{"label":"K","value":"k"}],"default":"auto","shown_when":{"present_effect":"dlss"},"description":"d"}]}],
             "presets":[]}
            """u8.ToArray());
        var schema = GraphicsSchema.Load(json);
        var settings = new GraphicsSettings(schema, Path.Combine(temp.Path, "g.toml"), temp.Path);
        var upscaler = schema.SettingRows.Single(r => r.Key == "present_effect");
        var preset = schema.SettingRows.Single(r => r.Key == "present_dlss_preset");
        Assert.True(settings.Shows(upscaler));
        Assert.False(settings.Shows(preset));
        settings.Set("present_effect", "dlss");
        Assert.True(settings.Shows(preset));
        settings.Set("present_effect", "fsr3");
        Assert.False(settings.Shows(preset));
    }

    private static GraphicsSettings UpscalerSettings(TempDir temp, string toml)
    {
        var json = temp.File("schema.json", """
            {"schema_version":1,"blocks":[{"rows":[
             {"label":"RESOLUTION SCALE","key":"resolution_scale","string":false,"kind":"choice","choices":[{"label":"1X","value":"1"},{"label":"2X","value":"2"},{"label":"3X","value":"3"},{"label":"4X","value":"4"}],"default":"1","description":"d"},
             {"label":"UPSCALER","key":"present_effect","string":true,"kind":"choice","choices":[{"label":"OFF","value":"bilinear"},{"label":"FSR 1","value":"fsr"},{"label":"FSR 2","value":"fsr2"},{"label":"DLSS","value":"dlss"}],"default":"bilinear","description":"d"}]}],
             "presets":[]}
            """u8.ToArray());
        return new GraphicsSettings(GraphicsSchema.Load(json), temp.File("g.toml", System.Text.Encoding.UTF8.GetBytes(toml)), temp.Path);
    }

    private static string[] Scales(GraphicsSettings settings, (int, int)? display) =>
        settings.ScaleChoices(display).Select(c => c.Value).ToArray();

    [Fact]
    public void An_upscaler_offers_only_the_scales_that_fit_the_display()
    {
        using var temp = new TempDir();
        var settings = UpscalerSettings(temp, "");
        Assert.Equal(["1", "2", "3", "4"], Scales(settings, (2560, 1440)));
        settings.Set("present_effect", "dlss");
        Assert.Equal(["1", "2"], Scales(settings, (2560, 1440)));
        Assert.Equal(["1", "2"], Scales(settings, (3413, 1920)));
        Assert.Equal(["1", "2", "3"], Scales(settings, (3840, 2160)));
        Assert.Equal(["1"], Scales(settings, (1920, 1080)));
        Assert.Equal(["1", "2", "3", "4"], Scales(settings, null));
        Assert.Equal(["1", "2", "3", "4"], Scales(settings, (1024, 768)));
    }

    [Fact]
    public void A_saved_larger_scale_stays_offered_and_runs_without_the_upscaler()
    {
        using var temp = new TempDir();
        var settings = UpscalerSettings(temp, "resolution_scale = 3\npresent_effect = \"dlss\"\n");
        Assert.Equal(["1", "2", "3"], Scales(settings, (2560, 1440)));
        Assert.Null(settings.TemporalUpscaler((2560, 1440)));
        Assert.Equal("DLSS", settings.TemporalUpscaler((3840, 2160)));
        Assert.Equal("DLSS", settings.TemporalUpscaler(null));
    }

    [Fact]
    public void Choosing_an_upscaler_lowers_a_scale_that_does_not_fit()
    {
        using var temp = new TempDir();
        var settings = UpscalerSettings(temp, "resolution_scale = 3\n");
        Assert.Null(settings.FitScaleToUpscaler((2560, 1440)));
        Assert.Null(settings.TemporalUpscaler((2560, 1440)));
        settings.Set("present_effect", "fsr2");
        Assert.Null(settings.FitScaleToUpscaler(null));
        Assert.Null(settings.FitScaleToUpscaler((1024, 768)));
        Assert.Equal(2, settings.FitScaleToUpscaler((2560, 1440)));
        Assert.Equal("2", settings.Value("resolution_scale"));
        Assert.Equal("FSR 2", settings.TemporalUpscaler((2560, 1440)));
        Assert.Null(settings.FitScaleToUpscaler((2560, 1440)));
    }

    [Fact]
    public void FSR_1_lowers_the_scale_like_the_others_but_leaves_the_anti_aliasing()
    {
        using var temp = new TempDir();
        var settings = UpscalerSettings(temp, "resolution_scale = 3\n");
        settings.Set("present_effect", "fsr");
        Assert.Equal(2, settings.FitScaleToUpscaler((2560, 1440)));
        Assert.Equal("FSR 1", settings.RunningUpscaler((2560, 1440)));
        Assert.Null(settings.TemporalUpscaler((2560, 1440)));
        settings.Set("present_effect", "fsr2");
        Assert.Equal("FSR 2", settings.RunningUpscaler((2560, 1440)));
        Assert.Equal("FSR 2", settings.TemporalUpscaler((2560, 1440)));
    }

    [Fact]
    public void A_version_without_the_upscaler_row_offers_every_scale()
    {
        using var temp = new TempDir();
        var json = temp.File("schema.json", """
            {"schema_version":1,"blocks":[{"rows":[
             {"label":"RESOLUTION SCALE","key":"resolution_scale","string":false,"kind":"choice","choices":[{"label":"1X","value":"1"},{"label":"2X","value":"2"},{"label":"3X","value":"3"}],"default":"1","description":"d"}]}],
             "presets":[]}
            """u8.ToArray());
        var settings = new GraphicsSettings(GraphicsSchema.Load(json), temp.File("g.toml", "resolution_scale = 3\npresent_effect = \"dlss\"\n"u8.ToArray()), temp.Path);
        Assert.Equal(["1", "2", "3"], Scales(settings, (2560, 1440)));
        Assert.Null(settings.FitScaleToUpscaler((2560, 1440)));
        Assert.Null(settings.TemporalUpscaler((2560, 1440)));
    }

    [Fact]
    public void Loads_the_schema_the_game_writes()
    {
        var real = Path.GetFullPath(Path.Combine(AppContext.BaseDirectory,
            @"..\..\..\..\..\out\build\win-amd64-release\graphics-settings.json"));
        if (!File.Exists(real)) return;
        var schema = GraphicsSchema.Load(real);
        Assert.Equal(3, schema.Blocks.Count);
        Assert.Equal(20, schema.SettingRows.Count());
        Assert.Equal(new Dictionary<string, string> { ["present_effect"] = "dlss" },
            schema.SettingRows.Single(r => r.Label == "DLSS PRESET").ShownWhen);
        Assert.Equal(["CONSOLE", "HIGH", "ULTRA", "EXPERIMENTAL"], schema.Presets.Select(p => p.Label));
        var labels = schema.Blocks.SelectMany(b => b.Rows).Select(r => r.Label).ToList();
        Assert.Contains("VSYNC", labels);
        Assert.Contains("CAR DIRT", labels);
        Assert.DoesNotContain("CAR SHADOWS", labels);
    }
}
