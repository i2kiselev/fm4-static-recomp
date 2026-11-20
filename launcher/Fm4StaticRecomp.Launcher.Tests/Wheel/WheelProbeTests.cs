// Copyright (c) 2026, i2kiselev
// BSD 3-Clause License: see LICENSE in the repository root.
using System.Diagnostics;
using Fm4StaticRecomp.Launcher.Core.Wheel;

namespace Fm4StaticRecomp.Launcher.Tests.Wheel;

public class WheelProbeTests
{
    // The SDK's own sample lines (tests/unit/input/joystick_probe_test.cpp).
    private const string Sample = """
        {"device":{"id":3,"name":"MOZA \"R5\" \\ Base","path":"\\\\?\\HID#VID_346E","vendor":"346e","product":"0004","wheel":true,"rest":[0,-32768],"buttons":64,"hats":1}}
        {"state":{"id":3,"axes":[12,-32768],"buttons":"0100","hats":[0]}}
        not a line
        {"removed":3}
        {"ready":true}
        """;

    [Fact]
    public void Parses_the_probe_lines()
    {
        var items = WheelProbe.ParseLines(new StringReader(Sample)).ToList();
        Assert.Equal(4, items.Count);
        var device = Assert.IsType<ProbeDevice>(items[0]);
        Assert.Equal(3u, device.Id);
        Assert.Equal("MOZA \"R5\" \\ Base", device.Name);
        Assert.Equal(@"\\?\HID#VID_346E", device.Path);
        Assert.Equal((0x346e, 0x0004), (device.Vendor, device.Product));
        Assert.Equal("346e:0004", device.Ids);
        Assert.True(device.Wheel);
        Assert.Equal([0, -32768], device.Rest);
        Assert.Equal((64, 1), (device.Buttons, device.Hats));
        var state = Assert.IsType<ProbeState>(items[1]);
        Assert.Equal([12, -32768], state.Axes);
        Assert.Equal([false, true, false, false], state.Buttons);
        Assert.Equal([0], state.Hats);
        Assert.Equal(3u, Assert.IsType<ProbeRemoved>(items[2]).Id);
        Assert.IsType<ProbeReady>(items[3]);
    }

    [Fact]
    public void Skips_malformed_lines()
    {
        var items = WheelProbe.ParseLines(new StringReader("{\"state\":{\"id\":\"x\"}}\n{\"device\":5}\n{\"ready\":true}\n")).ToList();
        Assert.IsType<ProbeReady>(Assert.Single(items));
    }

    [Fact]
    public async Task An_older_game_that_exits_at_once_asks_for_an_update()
    {
        using var probe = new WheelProbe(() => Shell("echo unknown option"));
        var ended = new TaskCompletionSource<string?>();
        probe.Ended += reason => ended.TrySetResult(reason);
        probe.Start();
        Assert.Equal(WheelProbe.UpdateMessage, await ended.Task.WaitAsync(TimeSpan.FromSeconds(3)));
    }

    [Fact]
    public async Task A_game_that_never_gets_ready_is_stopped()
    {
        using var probe = new WheelProbe(() => Shell(OperatingSystem.IsWindows() ? "ping -n 30 127.0.0.1 >nul" : "sleep 30"),
            readyTimeout: TimeSpan.FromMilliseconds(300));
        var ended = new TaskCompletionSource<string?>();
        probe.Ended += reason => ended.TrySetResult(reason);
        probe.Start();
        Assert.Equal(WheelProbe.UpdateMessage, await ended.Task.WaitAsync(TimeSpan.FromSeconds(5)));
    }

    [Fact]
    public void Raises_the_lines_as_events_and_stops_when_disposed()
    {
        using var temp = new TempDir();
        var lines = temp.File("lines.txt", """
            {"device":{"id":1,"name":"Pad","path":"p","vendor":"045e","product":"028e","wheel":false,"rest":[0],"buttons":2,"hats":0}}
            {"ready":true}

            """u8.ToArray());
        // Prints the lines, then waits for stdin to close, as the probe does. ArgumentList would escape
        // quotes in a way cmd does not read: the temp path has no spaces.
        var script = OperatingSystem.IsWindows() ? $"type {lines} & more" : $"cat '{lines}'; cat";
        var probe = new WheelProbe(() => Shell(script));
        var ready = new ManualResetEventSlim();
        ProbeDevice? added = null;
        string? endedReason = "none";
        probe.DeviceAdded += d => added = d;
        probe.Ready += ready.Set;
        probe.Ended += reason => endedReason = reason;
        probe.Start();
        Assert.True(ready.Wait(TimeSpan.FromSeconds(3)));
        Assert.Equal("Pad", added?.Name);
        var watch = Stopwatch.StartNew();
        probe.Dispose();
        Assert.True(watch.Elapsed < TimeSpan.FromSeconds(3));
        Assert.Equal("none", endedReason);
    }

    private static Process Shell(string script)
    {
        var start = OperatingSystem.IsWindows()
            ? new ProcessStartInfo("cmd.exe") { ArgumentList = { "/c", script } }
            : new ProcessStartInfo("/bin/sh") { ArgumentList = { "-c", script } };
        start.UseShellExecute = false;
        start.RedirectStandardInput = true;
        start.RedirectStandardOutput = true;
        start.CreateNoWindow = true;
        return Process.Start(start)!;
    }
}
