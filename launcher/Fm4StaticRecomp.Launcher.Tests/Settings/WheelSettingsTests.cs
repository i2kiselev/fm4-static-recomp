// Copyright (c) 2026, i2kiselev
// BSD 3-Clause License: see LICENSE in the repository root.
using System.Text;
using Fm4StaticRecomp.Launcher.Core.Settings;

namespace Fm4StaticRecomp.Launcher.Tests.Settings;

public class WheelSettingsTests
{
    private static WheelSettings Open(TempDir temp, string? text = null) =>
        new(temp.File("wheel.toml", text is null ? null : Encoding.UTF8.GetBytes(text)), WheelSchemaTests.Sample(), temp.File("backups"));

    private static string Saved(TempDir temp) => File.ReadAllText(temp.File("wheel.toml"));

    [Fact]
    public void Changes_one_button_and_keeps_the_others()
    {
        using var temp = new TempDir();
        var settings = Open(temp, "wheel_buttons = \"RB=13,LB=12,LS=7\"\n");
        Assert.Equal("13", settings.Button("shift_up"));
        settings.SetButton("shift_up", "14");
        settings.Save();
        Assert.Equal("wheel_buttons = \"RB=14,LB=12,LS=7\"\n", Saved(temp));
    }

    [Fact]
    public void Adds_and_removes_buttons_from_the_default_d_pad()
    {
        using var temp = new TempDir();
        var settings = Open(temp);
        Assert.Null(settings.Button("shift_up"));
        settings.SetButton("start", "346e:0004|37");
        settings.Save();
        Assert.Equal("wheel_buttons = \"UP=h0.up,DOWN=h0.down,LEFT=h0.left,RIGHT=h0.right,START=346e:0004|37\"\n", Saved(temp));
        settings.SetButton("start", null);
        settings.Save();
        Assert.Equal("", Saved(temp));
    }

    [Fact]
    public void Touching_an_advanced_value_makes_the_type_custom()
    {
        using var temp = new TempDir();
        var settings = Open(temp);
        settings.SetWheelType("belt");
        Assert.Equal(80, settings.Value("wheel_ffb_damper"));
        Assert.Equal("belt", settings.WheelType);
        settings.SetValue("wheel_ffb_strength", 70);
        Assert.Equal("belt", settings.WheelType);
        settings.SetValue("wheel_ffb_damper", 150);
        Assert.Equal("custom", settings.WheelType);
        settings.Save();
        Assert.Contains("wheel_type = \"custom\"\n", Saved(temp));
        Assert.Contains("wheel_ffb_damper = 150\n", Saved(temp));
    }

    [Fact]
    public void Steering_lock_keeps_the_type()
    {
        using var temp = new TempDir();
        var settings = Open(temp);
        settings.SetWheelType("gear");
        settings.SteeringLock = 540;
        Assert.Equal("gear", settings.WheelType);
    }

    [Fact]
    public void Reads_the_type_as_the_game_does()
    {
        using var temp = new TempDir();
        Assert.Equal("custom", Open(temp, "wheel_type = \"banana\"\nwheel_ffb_spring = 120\n").WheelType);
        Assert.Equal("direct", Open(temp, "wheel_type = \"banana\"\n").WheelType);
        Assert.Equal("direct", Open(temp).WheelType);
    }

    [Fact]
    public void Values_come_from_the_file_the_type_or_the_row()
    {
        using var temp = new TempDir();
        var settings = Open(temp, "wheel_type = \"gear\"\nwheel_ffb_spring = 999\nwheel_ffb_damper = soft\n");
        Assert.Equal(200, settings.Value("wheel_ffb_spring"));
        Assert.Equal(60, settings.Value("wheel_ffb_damper"));
        Assert.Equal(85, settings.Value("wheel_ffb_strength"));
    }

    [Fact]
    public void Clamps_a_value()
    {
        using var temp = new TempDir();
        var settings = Open(temp);
        settings.SetValue("wheel_ffb_jolt", 400);
        Assert.Equal(100, settings.Value("wheel_ffb_jolt"));
        settings.Save();
        Assert.Contains("wheel_ffb_jolt = 100\n", Saved(temp));
    }

    [Fact]
    public void Writes_a_pedal_axis()
    {
        using var temp = new TempDir();
        var settings = Open(temp);
        settings.SetAxis(WheelAxis.Throttle, "346e:0004|axis1");
        Assert.Equal("346e:0004|axis1", settings.Axis(WheelAxis.Throttle));
        Assert.Null(settings.Axis(WheelAxis.Clutch));
        settings.Save();
        Assert.Equal("wheel_throttle = \"346e:0004|axis1\"\n", Saved(temp));
    }

    [Fact]
    public void Steering_goes_to_device_axis_and_invert()
    {
        using var temp = new TempDir();
        var settings = Open(temp);
        settings.SetAxis(WheelAxis.Steering, "346e:0004|axis2");
        settings.SteeringInvert = true;
        Assert.Equal("346e:0004|axis2", settings.Axis(WheelAxis.Steering));
        settings.Save();
        Assert.Equal("wheel_device = \"346e:0004\"\nwheel_steering_axis = 2\nwheel_steering_invert = true\n", Saved(temp));
        settings.SetAxis(WheelAxis.Steering, null);
        settings.SteeringInvert = false;
        settings.Save();
        Assert.Equal("", Saved(temp));
    }

    [Fact]
    public void Steering_lock_follows_the_rotation()
    {
        using var temp = new TempDir();
        var settings = Open(temp, "wheel_rotation = 900\n");
        settings.SteeringLock = 540;
        settings.Save();
        Assert.Contains("wheel_steering_scale = 167\n", Saved(temp));
        Assert.Equal(540, Open(temp).SteeringLock);
        Assert.Equal(540, Open(temp).Value("steering_lock"));
        Assert.Equal(900, settings.Rotation);
        settings.SteeringLock = 2000;
        Assert.Equal(900, settings.SteeringLock);
    }

    [Fact]
    public void Expert_values_fall_back_to_the_schema_default()
    {
        using var temp = new TempDir();
        var settings = Open(temp, "wheel_ffb_trace = true\n");
        Assert.Equal("true", settings.Expert("wheel_ffb_trace"));
        Assert.Equal("difference", settings.Expert("wheel_ffb_mode"));
        settings.SetExpert("wheel_ffb_mode", "rumble");
        settings.SetExpert("wheel_ffb_trace", "false");
        settings.Save();
        Assert.Equal("wheel_ffb_mode = \"rumble\"\n", Saved(temp));
    }

    [Fact]
    public void Enabled_reads_and_writes_wheel_enabled()
    {
        using var temp = new TempDir();
        var settings = Open(temp);
        Assert.False(settings.Enabled);
        settings.Enabled = true;
        Assert.True(settings.Changed);
        settings.Save();
        Assert.Equal("wheel_enabled = true\n", Saved(temp));
        Assert.False(settings.Changed);
    }

    [Fact]
    public void Save_keeps_what_the_game_wrote_since_and_backs_up_once()
    {
        using var temp = new TempDir();
        var settings = Open(temp, "wheel_enabled = true\n");
        File.WriteAllText(temp.File("wheel.toml"), "wheel_enabled = true\nwheel_ffb_jolt = 60\n");
        settings.SetValue("wheel_ffb_strength", 70);
        settings.Save();
        Assert.Equal("wheel_enabled = true\nwheel_ffb_jolt = 60\nwheel_ffb_strength = 70\n", Saved(temp));
        settings.SetValue("wheel_ffb_strength", 75);
        settings.Save();
        Assert.Single(Directory.GetFiles(temp.File("backups"), "wheel-*.toml"));
    }

    [Fact]
    public void Keeps_the_extra_inputs_of_a_pad_button()
    {
        using var temp = new TempDir();
        var settings = Open(temp, "wheel_buttons = \"A=0,1,RB=13,LS=7,8\"\n");
        Assert.Equal("0", settings.Button("clutch"));
        settings.SetButton("shift_up", "14");
        settings.Save();
        Assert.Equal("wheel_buttons = \"A=0,1,RB=14,LS=7,8\"\n", Saved(temp));
        settings.SetButton("clutch", "5");
        settings.Save();
        Assert.Equal("wheel_buttons = \"A=5,RB=14,LS=7,8\"\n", Saved(temp));
    }

    [Fact]
    public void A_huge_number_reads_as_the_game_reads_it()
    {
        using var temp = new TempDir();
        var settings = Open(temp, "wheel_ffb_jolt = 99999999999\nwheel_steering_scale = 99999999999\n");
        Assert.Equal(100, settings.Value("wheel_ffb_jolt"));
        Assert.Equal(180, settings.SteeringLock);
    }

    [Fact]
    public void A_value_set_back_is_written_even_if_the_game_changed_the_file_since()
    {
        using var temp = new TempDir();
        var settings = Open(temp, "wheel_ffb_jolt = 30\n");
        File.WriteAllText(temp.File("wheel.toml"), "wheel_ffb_jolt = 70\n");
        settings.SetValue("wheel_ffb_jolt", 40);
        settings.SetValue("wheel_ffb_jolt", 30);
        settings.Save();
        Assert.Contains("wheel_ffb_jolt = 30\n", Saved(temp));
    }

    [Fact]
    public void A_cancelled_set_up_puts_the_edits_back()
    {
        using var temp = new TempDir();
        var settings = Open(temp);
        settings.SetValue("wheel_ffb_strength", 70);
        var mark = settings.Mark();
        settings.SetAxis(WheelAxis.Throttle, "axis1");
        settings.Revert(mark);
        Assert.Null(settings.Axis(WheelAxis.Throttle));
        Assert.Equal(70, settings.Value("wheel_ffb_strength"));
    }

    [Theory]
    [InlineData("0.3.3", false)]
    [InlineData("0.4.0", true)]
    [InlineData("dev", true)]
    public void Needs_a_game_with_wheel_settings(string version, bool supported) =>
        Assert.Equal(supported, WheelSettings.Supports(version));

    [Fact]
    public void Lists_the_actions_of_the_default_wheel_layout()
    {
        Assert.Equal(("shift_up", "Shift up", "RB", null), Row("shift_up"));
        Assert.Equal(("clutch", "Clutch", "A", "select"), Row("clutch"));
        Assert.Equal(("handbrake", "Handbrake", "B", "back"), Row("handbrake"));
        Assert.Equal("GEAR7", WheelActions.All.Single(a => a.Id == "gear7").Pad);
        Assert.Equal("REVERSE", WheelActions.All.Single(a => a.Id == "reverse").Pad);

        static (string, string, string, string?) Row(string id) =>
            WheelActions.All.Single(a => a.Id == id) is var a ? (a.Id, a.Label, a.Pad, a.MenuMeaning) : default;
    }
}
