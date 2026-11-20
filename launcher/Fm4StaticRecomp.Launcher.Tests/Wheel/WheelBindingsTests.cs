// Copyright (c) 2026, i2kiselev
// BSD 3-Clause License: see LICENSE in the repository root.
using Fm4StaticRecomp.Launcher.Core.Wheel;

namespace Fm4StaticRecomp.Launcher.Tests.Wheel;

public class WheelBindingsTests
{
    [Theory]
    [InlineData("346e:0004|axis1", "346e:0004", null, CaptureKind.Axis, 1, null)]
    [InlineData("-axis4", "", '-', CaptureKind.Axis, 4, null)]
    [InlineData("path:a|b|+a2", "path:a|b", '+', CaptureKind.Axis, 2, null)]
    [InlineData("346e:0004|13", "346e:0004", null, CaptureKind.Button, 13, null)]
    [InlineData("b7", "", null, CaptureKind.Button, 7, null)]
    [InlineData("Pedals|h0.down", "Pedals", null, CaptureKind.Hat, 0, "down")]
    public void Parses_the_sdk_forms(string text, string device, char? sign, CaptureKind kind, int index, string? hat)
    {
        Assert.Equal(new WheelBinding(device, sign, kind, index, hat), WheelBinding.Parse(text));
    }

    [Theory]
    [InlineData("axis")]
    [InlineData("dev|h0")]
    [InlineData("dev|x")]
    public void Refuses_what_the_sdk_refuses(string text) => Assert.Null(WheelBinding.Parse(text));

    [Theory]
    [InlineData(-32768, -32768, null, 0)]
    [InlineData(32767, -32768, null, 100)]
    [InlineData(32767, 32767, null, 0)]
    [InlineData(-32768, 32767, null, 100)]
    [InlineData(32767, 32767, '+', 100)]
    [InlineData(-32768, -32768, '-', 100)]
    public void A_pedal_reads_from_its_rest_unless_a_sign_says_otherwise(short raw, short rest, char? sign, int percent)
    {
        Assert.Equal(percent, WheelBinding.PedalPercent(raw, rest, sign));
    }
}
