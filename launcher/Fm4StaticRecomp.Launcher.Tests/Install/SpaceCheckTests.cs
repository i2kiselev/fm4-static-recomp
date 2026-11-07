// Copyright (c) 2026, i2kiselev
// BSD 3-Clause License: see LICENSE in the repository root.
using Fm4StaticRecomp.Launcher.Core.Install;
using Fm4StaticRecomp.Launcher.Core.Pfp4;

namespace Fm4StaticRecomp.Launcher.Tests.Install;

public class SpaceCheckTests
{
    [Fact]
    public void Adds_the_parts_still_to_be_written_and_the_margin()
    {
        Assert.Equal(7_600_000_000 + 2_800_000_000 + 2_000_000_000 + 1_200_000_000 + 2_000_000_000L, SpaceCheck.Needed(true, true, true, true, false));
        Assert.Equal(1_200_000_000 + 2_000_000_000L, SpaceCheck.Needed(false, false, false, true, false));
    }

    [Fact]
    public void Names_both_figures_when_space_is_short()
    {
        var needed = SpaceCheck.Needed(true, true, true, true, false);
        Assert.Null(SpaceCheck.Problem(@"C:\G\data", needed, _ => needed));
        Assert.Contains("needs 15.6 GB, 9.0 GB free", SpaceCheck.Problem(@"C:\G\data", needed, _ => 9_000_000_000)!);
    }

    [Fact]
    public void A_pfp4_install_needs_its_content_space()
    {
        var real = Path.GetFullPath(Path.Combine(AppContext.BaseDirectory, "../../../../../config/pfp4-1.3.json"));
        Assert.True(SpaceCheck.Pfp4Content >= Pfp4Installer.ContentBytes(Pfp4Manifest.Load(real)));
        Assert.Equal(SpaceCheck.Pfp4Content, SpaceCheck.Needed(true, false, true, true, true) - SpaceCheck.Needed(true, false, true, true, false));
    }
}
