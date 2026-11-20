// Copyright (c) 2026, i2kiselev
// BSD 3-Clause License: see LICENSE in the repository root.
using Fm4StaticRecomp.Launcher.Core.Wheel;

namespace Fm4StaticRecomp.Launcher.Tests.Wheel;

public class WheelDevicesTests
{
    private static ProbeDevice Device(uint id, string name, ushort vendor, ushort product, string path) =>
        new(id, name, path, vendor, product, Wheel: true, Rest: [0], Buttons: 0, Hats: 0);

    [Fact]
    public void Two_identical_devices_are_told_apart_by_path()
    {
        var left = Device(1, "MOZA SR-P", 0x346e, 0x0004, "path-a");
        var right = Device(2, "MOZA SR-P", 0x346e, 0x0004, "path-b");
        IReadOnlyList<ProbeDevice> all = [left, right];
        Assert.Equal("path:path-a", WheelDevices.Match(left, all));
        Assert.Equal("path:path-b", WheelDevices.Match(right, all));
        Assert.Same(right, WheelDevices.Find("path:path-b", all));
    }

    [Fact]
    public void One_device_is_named_by_its_usb_ids()
    {
        var wheel = Device(1, "MOZA R5 Base", 0x346e, 0x0004, "p");
        Assert.Equal("346e:0004", WheelDevices.Match(wheel, [wheel]));
    }

    [Fact]
    public void A_device_without_ids_is_named_by_a_unique_name()
    {
        var wheel = Device(1, "Virtual Wheel", 0, 0, "p1");
        var pad = Device(2, "Pad", 0, 0, "p2");
        Assert.Equal("Virtual Wheel", WheelDevices.Match(wheel, [wheel, pad]));
    }

    [Fact]
    public void The_wheel_base_found_automatically_is_not_a_pedal_set()
    {
        var pedals = Device(1, "MOZA SR-P Pedals", 0x346e, 0x0003, "p1");
        var shifter = Device(2, "MOZA HGP Shifter", 0x346e, 0x0005, "p2");
        var wheel = Device(3, "MOZA R5 Base", 0x346e, 0x0004, "p3");
        Assert.Same(wheel, WheelDevices.SteeringCandidate([pedals, shifter, wheel]));
        Assert.Null(WheelDevices.SteeringCandidate([pedals, shifter]));
    }

    [Fact]
    public void Matches_as_the_sdk_does()
    {
        var wheel = Device(1, "MOZA R5 Base", 0x346e, 0x0004, "p");
        Assert.True(WheelDevices.Matches("", wheel));
        Assert.True(WheelDevices.Matches("346E:0004", wheel));
        Assert.True(WheelDevices.Matches("r5 base", wheel));
        Assert.True(WheelDevices.Matches("PATH:p", wheel));
        Assert.False(WheelDevices.Matches("path:q", wheel));
        Assert.False(WheelDevices.Matches("044f:b66e", wheel));
    }
}
