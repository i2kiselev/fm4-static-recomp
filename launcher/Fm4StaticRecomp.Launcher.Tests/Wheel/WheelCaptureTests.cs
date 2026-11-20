// Copyright (c) 2026, i2kiselev
// BSD 3-Clause License: see LICENSE in the repository root.
using Fm4StaticRecomp.Launcher.Core.Wheel;

namespace Fm4StaticRecomp.Launcher.Tests.Wheel;

public class WheelCaptureTests
{
    internal static readonly ProbeDevice R5 =
        new(3, "MOZA R5 Base", "r5", 0x346e, 0x0004, Wheel: true, Rest: [0, -32768, 0, 0, -32768], Buttons: 4, Hats: 1);

    internal static ProbeState State(short[] axes, string buttons = "0000", byte hat = 0, uint id = 3) =>
        new(id, axes, buttons.Select(c => c == '1').ToList(), [hat]);

    [Fact]
    public void A_pedal_resting_at_full_scale_is_found_without_a_sign()
    {
        var capture = WheelCapture.ForAxis(steering: false, taken: []);
        capture.Feed(State([0, -32768, 0, 0, -32768]), [R5]);
        capture.Feed(State([300, -20000, 0, 0, -32768]), [R5]);
        Assert.Null(capture.Result);
        capture.Feed(State([300, 20000, 0, 0, -32768]), [R5]);
        var result = Assert.IsType<CaptureResult>(capture.Result);
        Assert.Equal((CaptureKind.Axis, 1, (short)-32768), (result.Kind, result.Index, result.Rest));
        Assert.Equal("346e:0004|axis1", result.Binding);
    }

    [Theory]
    [InlineData(-20000, false)]
    [InlineData(20000, true)]
    public void Steering_turned_left_gives_the_invert(short moved, bool inverted)
    {
        var capture = WheelCapture.ForAxis(steering: true, taken: []);
        capture.Feed(State([0, -32768, 0, 0, -32768]), [R5]);
        capture.Feed(State([moved, -32768, 0, 0, -32768]), [R5]);
        var result = Assert.IsType<CaptureResult>(capture.Result);
        Assert.Equal(0, result.Index);
        Assert.Equal(inverted, result.Positive);
    }

    [Fact]
    public void Skips_axes_already_taken_and_takes_the_largest_move()
    {
        var capture = WheelCapture.ForAxis(steering: false, taken: [(3u, 1)]);
        capture.Feed(State([0, -32768, 0, 0, -32768]), [R5]);
        capture.Feed(State([0, 30000, 17000, 0, 0]), [R5]);
        Assert.Equal(4, capture.Result!.Index);
    }

    [Fact]
    public void A_button_held_since_the_start_counts_only_when_pressed_again()
    {
        var capture = WheelCapture.ForButton();
        capture.Feed(State([0, 0, 0, 0, 0], "0100"), [R5]);
        capture.Feed(State([0, 0, 0, 0, 0], "0100"), [R5]);
        Assert.Null(capture.Result);
        capture.Feed(State([0, 0, 0, 0, 0], "0000"), [R5]);
        capture.Feed(State([0, 0, 0, 0, 0], "0100"), [R5]);
        Assert.Equal((CaptureKind.Button, 1), (capture.Result!.Kind, capture.Result.Index));
        Assert.Equal("346e:0004|1", capture.Result.Binding);
    }

    [Fact]
    public void A_hat_direction_is_a_button()
    {
        var capture = WheelCapture.ForButton();
        capture.Feed(State([0, 0, 0, 0, 0]), [R5]);
        capture.Feed(State([0, 0, 0, 0, 0], hat: 4), [R5]);
        Assert.Equal("346e:0004|h0.down", capture.Result!.Binding);
    }

    [Fact]
    public void A_button_capture_ignores_axes_and_the_handbrake_takes_either()
    {
        var button = WheelCapture.ForButton();
        var handbrake = WheelCapture.ForAxisOrButton(taken: []);
        foreach (var capture in new[] { button, handbrake })
        {
            capture.Feed(State([0, -32768, 0, 0, -32768]), [R5]);
            capture.Feed(State([0, -32768, 0, 30000, -32768]), [R5]);
        }
        Assert.Null(button.Result);
        Assert.Equal((CaptureKind.Axis, 3), (handbrake.Result!.Kind, handbrake.Result.Index));
    }

    [Fact]
    public void Names_one_of_two_identical_pedal_sets_by_its_path()
    {
        var left = R5 with { Id = 4, Path = "pedals-a" };
        var right = R5 with { Id = 5, Path = "pedals-b" };
        var capture = WheelCapture.ForAxis(steering: false, taken: []);
        capture.Feed(State([0, -32768, 0, 0, -32768], id: 4), [left, right]);
        capture.Feed(State([0, -32768, 0, 0, -32768], id: 5), [left, right]);
        capture.Feed(State([0, 20000, 0, 0, -32768], id: 5), [left, right]);
        Assert.Equal("path:pedals-b|axis1", capture.Result!.Binding);
        Assert.Equal(5u, capture.Result.DeviceId);
    }
}
