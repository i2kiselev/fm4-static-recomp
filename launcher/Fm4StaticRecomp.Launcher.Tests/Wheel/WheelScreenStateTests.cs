// Copyright (c) 2026, i2kiselev
// BSD 3-Clause License: see LICENSE in the repository root.
using Fm4StaticRecomp.Launcher.Core.Wheel;

namespace Fm4StaticRecomp.Launcher.Tests.Wheel;

public class WheelScreenStateTests
{
    [Fact]
    public void A_running_game_leaves_only_force_feedback_editable()
    {
        Assert.Equal((false, true, "Close the game to change the wheel set-up."),
            WheelScreenState.For(gameRunning: true, gameBuilt: true, probeEnded: false));
    }

    [Fact]
    public void Nothing_is_editable_before_the_game_is_built()
    {
        Assert.Equal((false, false, "Build the game first."), WheelScreenState.For(gameRunning: false, gameBuilt: false, probeEnded: false));
        Assert.Equal((false, false, "Build the game first."), WheelScreenState.For(gameRunning: true, gameBuilt: false, probeEnded: true));
    }

    [Fact]
    public void An_ended_probe_asks_for_an_update()
    {
        Assert.Equal((false, true, "Update the game to set up a wheel."),
            WheelScreenState.For(gameRunning: false, gameBuilt: true, probeEnded: true));
    }

    [Fact]
    public void Everything_is_editable_otherwise()
    {
        Assert.Equal((true, true, null), WheelScreenState.For(gameRunning: false, gameBuilt: true, probeEnded: false));
    }
}
