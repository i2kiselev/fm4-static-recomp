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
        var real = Path.GetFullPath(Path.Combine(AppContext.BaseDirectory, @"..\..\..\..\..\config\supported-discs.json"));
        var manifest = DiscManifest.Load(real);
        Assert.Equal(["61D62D9F", "26FF1101", "42C67824", "33E7B39F"], manifest.Disc1.Releases.Select(r => r.MediaId));
        Assert.Equal(4001, manifest.Disc1.Files.Count);
        Assert.Equal(4, manifest.Disc2.Packages.Count);
        foreach (var release in manifest.Disc1.Releases)
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
}
