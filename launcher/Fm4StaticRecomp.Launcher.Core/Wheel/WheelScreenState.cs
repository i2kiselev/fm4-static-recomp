// Copyright (c) 2026, i2kiselev
// BSD 3-Clause License: see LICENSE in the repository root.
namespace Fm4StaticRecomp.Launcher.Core.Wheel;

// Which parts of WHEEL can change. SETUP and BUTTONS need the probe, which the running game would
// fight for the devices; FORCE FEEDBACK is only the file.
public static class WheelScreenState
{
    public const string BuildFirst = "Build the game first.", CloseGame = "Close the game to change the wheel set-up.";

    public static (bool SetupEditable, bool FfbEditable, string? Message) For(bool gameRunning, bool gameBuilt, bool probeEnded)
    {
        if (!gameBuilt) return (false, false, BuildFirst);
        if (gameRunning) return (false, true, CloseGame);
        if (probeEnded) return (false, true, WheelProbe.UpdateMessage);
        return (true, true, null);
    }
}
