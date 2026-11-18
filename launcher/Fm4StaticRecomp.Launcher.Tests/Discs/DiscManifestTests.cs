// Copyright (c) 2026, i2kiselev
// BSD 3-Clause License: see LICENSE in the repository root.
using Fm4StaticRecomp.Launcher.Core.Discs;

namespace Fm4StaticRecomp.Launcher.Tests.Discs;

public class DiscManifestTests
{
    [Fact]
    public void Reads_releases_and_shared_files()
    {
        using var temp = new TempDir();
        var manifest = DiscManifest.Load(TestDiscs.WriteManifest(temp, TestDiscs.Pal()));
        Assert.Equal(["1.0 PAL (Europe, Australia)", "1.0 USA", "Rev 1 Europe", "Rev 1 USA"],
            manifest.Disc1.Releases.Select(r => r.Name));
        var pal = manifest.Release("61d62d9f")!;
        Assert.Equal(["$SystemUpdate/su.bin", "Media/a.bin", "Media/b.bin", "default.xex"],
            manifest.Disc1Files(pal).Select(f => f.Path).Order(StringComparer.Ordinal));
        Assert.True(manifest.Disc1Files(pal).Single(f => f.Path.StartsWith('$')).Optional);
        Assert.False(manifest.Disc1Files(pal).Single(f => f.Path == "Media/a.bin").Optional);
        Assert.Null(manifest.Release("DEADBEEF"));
    }

    [Fact]
    public void Refuses_another_schema()
    {
        using var temp = new TempDir();
        var path = temp.File("old.json", """{"schema_version": 1, "title_id": "4D530910", "disc1": {"media_id": "61D62D9F"}}"""u8.ToArray());
        Assert.Throws<InvalidDataException>(() => DiscManifest.Load(path));
    }

    [Fact]
    public void Loads_the_real_manifest()
    {
        var real = Path.GetFullPath(Path.Combine(AppContext.BaseDirectory, "../../../../../config/supported-discs.json"));
        var manifest = DiscManifest.Load(real);
        Assert.Equal(["61D62D9F", "26FF1101", "42C67824", "33E7B39F", "633772A0"], manifest.DiscReleases.Select(r => r.MediaId));
        Assert.Equal(4001, manifest.Disc1.Files.Count);
        Assert.Equal(4, manifest.Disc2.Packages.Count);
        foreach (var release in manifest.DiscReleases)
        {
            // The installer keys the files by path, ignoring case: one entry per path.
            var files = manifest.Disc1Files(release);
            Assert.Equal(files.Count, files.Select(f => f.Path).Distinct(StringComparer.OrdinalIgnoreCase).Count());
            Assert.Single(files, f => f.Path == "default.xex");
        }
        var rev1 = manifest.Release("42C67824")!;
        Assert.Contains(manifest.Disc1Files(rev1), f => f.Path == "Media/tracks/LeMans/patch_bin.zip");
        Assert.DoesNotContain(manifest.Disc1Files(manifest.Release("61D62D9F")!), f => f.Path == "Media/tracks/LeMans/patch_bin.zip");
    }

    [Fact]
    public void The_mod_release_is_not_a_disc_release()
    {
        var real = Path.GetFullPath(Path.Combine(AppContext.BaseDirectory, "../../../../../config/supported-discs.json"));
        var manifest = DiscManifest.Load(real);
        Assert.Equal("Project Forza Plus 4 1.3", manifest.ModRelease.Name);
        Assert.Equal("633772A0", manifest.ModRelease.MediaId);
        Assert.Equal("FEA11E87FF8434E51ED8E683023EA17281DEC02A5ED8FA226B1C73726CF15128",
            manifest.ModRelease.Files.Single(f => f.Path == "default.xex").Sha256);
        Assert.DoesNotContain(manifest.DiscReleases, r => r.Mod);
        var asia = manifest.Release("633772A0")!;
        Assert.Equal("1.0 Asia", asia.Name);
        Assert.False(asia.Mod);
        Assert.Equal("763636FE8CC9D2FF822ADE4C972EFEF907E193F31175DEF127863575BF3C4AD5",
            asia.Files.Single(f => f.Path == "default.xex").Sha256);
    }

    [Theory]
    [InlineData(false)]
    [InlineData(true)]
    public async Task Disc_1_setup_distinguishes_retail_Asia_from_the_mod(bool modded)
    {
        var real = Path.GetFullPath(Path.Combine(AppContext.BaseDirectory, "../../../../../config/supported-discs.json"));
        var manifest = DiscManifest.Load(real);
        var asia = manifest.Release("633772A0")!;
        var mod = manifest.ModRelease;
        var retailXex = TestDiscs.Xex(0x633772A0);
        var modXex = TestDiscs.Xex(0x633772A0, extra: 1);
        ManifestFile Entry(byte[] data) => new("default.xex", data.LongLength, TestDiscs.Sha(data));
        var fixture = manifest with
        {
            Disc1 = new Disc1Manifest([mod with { Files = [Entry(modXex)] }, asia with { Files = [Entry(retailXex)] }], []),
        };
        using var temp = new TempDir();
        temp.File("disc/default.xex", modded ? modXex : retailXex);
        var input = DiscInput.Detect(Path.Combine(temp.Path, "disc"))!;
        var game = Path.Combine(temp.Path, "game");
        var outcome = await Disc1Step.InstallAsync(input, fixture, game, _ => Task.FromResult(false), null,
            CancellationToken.None);

        if (modded)
        {
            var refused = Assert.IsType<Disc1Outcome.Refused>(outcome);
            Assert.Equal("633772A0", refused.Check.MediaId);
            Assert.Contains("Project Forza Plus 4", Disc1Step.RefusalText(refused.Check, fixture));
            Assert.False(Directory.Exists(game));
        }
        else
        {
            Assert.Equal(new Disc1Outcome.Installed("1.0 Asia", 0), outcome);
            Assert.Equal(retailXex, File.ReadAllBytes(Path.Combine(game, "default.xex")));
        }
    }
}
