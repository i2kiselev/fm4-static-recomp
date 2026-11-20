// Copyright (c) 2026, i2kiselev
// BSD 3-Clause License: see LICENSE in the repository root.
using Fm4StaticRecomp.Launcher.Core.Settings;
using Fm4StaticRecomp.Launcher.Core.Wheel;
using Fm4StaticRecomp.Launcher.Tests.Settings;

namespace Fm4StaticRecomp.Launcher.Tests.Wheel;

public class WheelGuideTests
{
    private static CaptureResult Axis(int index, bool positive = true) =>
        new("346e:0004", 3, CaptureKind.Axis, index, Rest: 0, Positive: positive);

    private static CaptureResult Button(int index) => new("346e:0004", 3, CaptureKind.Button, index, Rest: 0, Positive: true);

    private static WheelSettings Settings(TempDir temp, string text = "") =>
        new(temp.File("wheel.toml", System.Text.Encoding.UTF8.GetBytes(text)), WheelSchemaTests.Sample(), temp.File("backups"));

    [Fact]
    public void Skipping_clutch_and_handbrake_leaves_them_unset()
    {
        using var temp = new TempDir();
        var settings = Settings(temp, "wheel_clutch = \"axis2\"\nwheel_handbrake = \"axis3\"\n");
        var guide = new WheelGuide(settings);
        Assert.Equal("Turn the wheel all the way to the left.", guide.Prompt);
        guide.Accept(Axis(0, positive: false));
        Assert.Equal("Press the throttle pedal all the way down.", guide.Prompt);
        guide.Accept(Axis(1));
        guide.Accept(Axis(4));
        Assert.Equal(WheelGuideStep.Clutch, guide.Current);
        Assert.True(guide.CanSkip);
        guide.Skip();
        Assert.Equal("Pull the handbrake, or press SKIP.", guide.Prompt);
        guide.Skip();
        foreach (var index in new[] { 13, 12, 37, 0, 1 })
        {
            Assert.False(guide.Done);
            Assert.False(guide.CanSkip);
            guide.Accept(Button(index));
        }
        Assert.True(guide.Done);
        Assert.Null(settings.Axis(WheelAxis.Clutch));
        Assert.Null(settings.Axis(WheelAxis.Handbrake));
        Assert.Equal("346e:0004|axis0", settings.Axis(WheelAxis.Steering));
        Assert.False(settings.SteeringInvert);
        Assert.Equal("346e:0004|axis1", settings.Axis(WheelAxis.Throttle));
        Assert.Equal("346e:0004|axis4", settings.Axis(WheelAxis.Brake));
        Assert.Equal("346e:0004|13", settings.Button("shift_up"));
        Assert.Equal("346e:0004|12", settings.Button("shift_down"));
        Assert.Equal("346e:0004|37", settings.Button("start"));
        Assert.Equal("346e:0004|0", settings.Button("clutch"));
        Assert.Equal("346e:0004|1", settings.Button("handbrake"));
    }

    [Fact]
    public void A_handbrake_button_answers_menu_back()
    {
        using var temp = new TempDir();
        var settings = Settings(temp);
        var guide = new WheelGuide(settings);
        guide.Accept(Axis(0));
        Assert.True(settings.SteeringInvert);
        guide.Accept(Axis(1));
        guide.Accept(Axis(4));
        guide.Accept(Axis(2));
        guide.Accept(Button(5));
        guide.Accept(Button(13));
        guide.Accept(Button(12));
        guide.Accept(Button(37));
        Assert.Equal(WheelGuideStep.MenuSelect, guide.Current);
        guide.Accept(Button(0));
        Assert.True(guide.Done);
        Assert.Equal("346e:0004|5", settings.Button("handbrake"));
        Assert.Equal("346e:0004|axis2", settings.Axis(WheelAxis.Clutch));
    }

    [Fact]
    public void Pedal_captures_skip_the_axes_taken_by_earlier_steps()
    {
        using var temp = new TempDir();
        var guide = new WheelGuide(Settings(temp));
        guide.Accept(Axis(0));
        guide.Accept(Axis(1));
        var capture = guide.NewCapture();
        capture.Feed(WheelCaptureTests.State([0, -32768, 0, 0, -32768]), [WheelCaptureTests.R5]);
        capture.Feed(WheelCaptureTests.State([30000, 30000, 0, 0, -32768]), [WheelCaptureTests.R5]);
        Assert.Null(capture.Result);
    }
}
