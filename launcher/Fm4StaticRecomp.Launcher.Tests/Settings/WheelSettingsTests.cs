// Copyright (c) 2026, FugaziScript
// BSD 3-Clause License: see LICENSE in the repository root.
using Fm4StaticRecomp.Launcher.Core.Settings;

namespace Fm4StaticRecomp.Launcher.Tests.Settings;

public class WheelSettingsTests
{
    [Fact]
    public void Defaults_are_the_games_and_changes_are_written_with_the_prefix()
    {
        using var dir = new TempDir();
        var path = dir.File("config/wheel.toml");
        var wheel = new WheelSettings(path);
        Assert.False(wheel.GetBool("enabled"));
        Assert.Equal(25, wheel.GetInt("ffb_strength"));
        Assert.Equal("B", wheel.Get("clutch_button"));

        wheel.Set("enabled", true);
        wheel.Set("throttle", "SimPedals|-axis1");
        wheel.Set("ffb_strength", 60);
        var text = File.ReadAllText(path);
        Assert.Contains("fm4_wheel_enabled = true", text);
        Assert.Contains("fm4_wheel_throttle = \"SimPedals|-axis1\"", text);
        Assert.Contains("fm4_wheel_ffb_strength = 60", text);
        Assert.Equal("SimPedals|-axis1", new WheelSettings(path).Get("throttle"));
        Assert.Throws<KeyNotFoundException>(() => wheel.Set("not_a_setting", "1"));
    }

    [Fact]
    public void Lines_it_does_not_change_are_kept()
    {
        using var dir = new TempDir();
        var path = dir.File("wheel.toml", "# mine\nfm4_wheel_log_inputs = true\nsomething_else = 3\n"u8.ToArray());
        new WheelSettings(path).Set("device", "0eb7:0004");
        var text = File.ReadAllText(path);
        Assert.StartsWith("# mine\nfm4_wheel_log_inputs = true\nsomething_else = 3\n", text);
        Assert.Contains("fm4_wheel_device = \"0eb7:0004\"", text);
    }

    [Fact]
    public void Buttons_bind_replace_add_and_clear()
    {
        using var dir = new TempDir();
        var wheel = new WheelSettings(dir.File("wheel.toml"));
        Assert.Equal(("UP", "h0.up"), wheel.Buttons()[0]);
        wheel.BindButton("A", "3", add: false);
        wheel.BindButton("A", "Box|5", add: true);
        wheel.BindButton("UP", "7", add: false);
        Assert.Equal("DOWN=h0.down,LEFT=h0.left,RIGHT=h0.right,A=3,A=Box|5,UP=7", wheel.Get("buttons"));
        wheel.ClearButton("A");
        Assert.DoesNotContain(wheel.Buttons(), b => b.Name == "A");
        Assert.Empty(WheelSettings.ParseButtons("=1,A=,nonsense"));
    }

    [Theory]
    [InlineData(100, 900, 0, 100)]
    [InlineData(100, 900, 360, 250)]
    [InlineData(50, 900, 360, 125)]
    [InlineData(100, 2520, 90, 1000)]
    public void Steering_scale_matches_the_game(int scale, int wheel, int game, int expected) =>
        Assert.Equal(expected, WheelSettings.SteeringScale(scale, wheel, game));
}

public class WheelCaptureTests
{
    private static WheelDevice Device(string match, string path, int[] axes, bool[] buttons, int[] hats) =>
        new(match, match, path, "0000:0000", false, axes, axes, buttons, hats);

    private static WheelSnapshot Snapshot(string baseMatch, params WheelDevice[] devices) =>
        new(true, "", "", false, baseMatch, devices, new WheelPad(0, 0, 0, 0, 0, 0));

    [Fact]
    public void A_pedal_on_another_device_binds_with_its_name()
    {
        var start = Snapshot("Base", Device("Base", "a", [0], [false], [0]), Device("Pedals", "b", [-32768, -32768], [], []));
        var capture = new WheelCapture(start, axis: true);
        Assert.Null(capture.Next(Snapshot("Base", Device("Base", "a", [3000], [false], [0]),
            Device("Pedals", "b", [-32768, -30000], [], []))));
        Assert.Equal("Pedals|axis1", capture.Next(Snapshot("Base", Device("Base", "a", [0], [false], [0]),
            Device("Pedals", "b", [-32768, 20000], [], []))));
    }

    [Fact]
    public void Buttons_hats_and_levers_bind_on_the_wheel_base_without_its_name()
    {
        var start = Snapshot("Base", Device("Base", "a", [0, -32768], [false, false], [0]));
        Assert.Equal("1", new WheelCapture(start, axis: false).Next(
            Snapshot("Base", Device("Base", "a", [0, -32768], [false, true], [0]))));
        Assert.Equal("h0.left", new WheelCapture(start, axis: false).Next(
            Snapshot("Base", Device("Base", "a", [0, -32768], [false, false], [8]))));
        Assert.Equal("+axis1", new WheelCapture(start, axis: false).Next(
            Snapshot("Base", Device("Base", "a", [0, 32767], [false, false], [0]))));
        // An axis capture ignores buttons.
        Assert.Null(new WheelCapture(start, axis: true).Next(
            Snapshot("Base", Device("Base", "a", [0, -32768], [true, true], [1]))));
    }

    [Fact]
    public void Bindings_split_into_device_and_axis()
    {
        Assert.Equal(2, WheelCapture.AxisIndex("0eb7:0004|axis2"));
        Assert.Equal(0, WheelCapture.AxisIndex("-axis0"));
        Assert.Null(WheelCapture.AxisIndex("h0.up"));
        Assert.Equal("path:\\\\?\\hid#x|y", WheelCapture.DeviceOf("path:\\\\?\\hid#x|y|axis0"));
        Assert.Equal("", WheelCapture.DeviceOf("axis0"));
    }

    [Theory]
    // Without a sign the direction comes from where the pedal rests, as in the game.
    [InlineData("axis1", -32768, false, "-axis1")]
    [InlineData("axis1", 32767, true, "+axis1")]
    [InlineData("axis1", null, false, "-axis1")]
    [InlineData("Pedals|axis2", -32768, false, "Pedals|-axis2")]
    [InlineData("Pedals|-axis2", -32768, true, "Pedals|+axis2")]
    [InlineData("path:\\\\?\\hid#x|+axis0", 32767, false, "path:\\\\?\\hid#x|-axis0")]
    [InlineData("h0.up", null, false, "h0.up")]
    public void A_pedal_reverses(string binding, int? rest, bool reversed, string expected)
    {
        Assert.Equal(reversed, WheelCapture.IsReversed(binding, rest));
        Assert.Equal(expected, WheelCapture.Reverse(binding, rest));
        if (binding != expected) Assert.Equal(!reversed, WheelCapture.IsReversed(expected, rest));
    }

    [Fact]
    public void The_probe_line_parses()
    {
        const string line = "{\"ready\":true,\"status\":\"ready\",\"ffb_device\":\"Base\",\"test_busy\":false,\"base\":\"346e:0006\","
            + "\"devices\":[{\"match\":\"346e:0006\",\"name\":\"Base\",\"path\":\"p\",\"ids\":\"346e:0006\",\"wheel\":true,"
            + "\"axes\":[1,-2],\"rest\":[0,0],\"buttons\":[0,1],\"hats\":[4]}],"
            + "\"pad\":{\"steering\":-100,\"throttle\":255,\"brake\":3,\"clutch\":0,\"buttons\":4096,\"gear\":2}}";
        var snapshot = WheelSnapshot.Parse(line)!;
        Assert.True(snapshot.Ready);
        Assert.Equal("346e:0006", snapshot.Base);
        Assert.Equal(new[] { false, true }, snapshot.Devices[0].Buttons);
        Assert.Equal(-100, snapshot.Pad.Steering);
        Assert.Equal(2, snapshot.Pad.Gear);
        Assert.Null(WheelSnapshot.Parse("[info] a log line"));
        Assert.Null(WheelSnapshot.Parse("{\"ready\":true}"));
    }
}
