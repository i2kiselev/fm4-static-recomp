using Fm4StaticRecomp.Launcher.Core.Install;

namespace Fm4StaticRecomp.Launcher.Tests.Install;

public class SpaceCheckTests
{
    [Fact]
    public void Adds_the_parts_still_to_be_written_and_the_margin()
    {
        Assert.Equal(7_600_000_000 + 2_800_000_000 + 2_000_000_000 + 1_200_000_000 + 2_000_000_000L, SpaceCheck.Needed(true, true, true, true));
        Assert.Equal(1_200_000_000 + 2_000_000_000L, SpaceCheck.Needed(false, false, false, true));
    }

    [Fact]
    public void Names_both_figures_when_space_is_short()
    {
        var needed = SpaceCheck.Needed(true, true, true, true);
        Assert.Null(SpaceCheck.Problem(@"C:\G\data", needed, _ => needed));
        Assert.Contains("needs 15.6 GB, 9.0 GB free", SpaceCheck.Problem(@"C:\G\data", needed, _ => 9_000_000_000)!);
    }
}
