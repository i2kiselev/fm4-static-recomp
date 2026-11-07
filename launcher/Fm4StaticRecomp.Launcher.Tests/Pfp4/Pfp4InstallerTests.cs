// Copyright (c) 2026, i2kiselev
// BSD 3-Clause License: see LICENSE in the repository root.
using System.Buffers.Binary;
using System.Text;
using Fm4StaticRecomp.Launcher.Core.Discs;
using Fm4StaticRecomp.Launcher.Core.Install;
using Fm4StaticRecomp.Launcher.Core.Pfp4;
using Fm4StaticRecomp.Launcher.Tests.Discs;

namespace Fm4StaticRecomp.Launcher.Tests.Pfp4;

public class Pfp4InstallerTests
{
    private static InstallLayout Layout(TempDir temp, string? root = null)
    {
        var layout = new InstallLayout(root ?? TestPath.Combine(temp.Path, "data"));
        temp.File(TestPath.Combine(layout.Game, "default.xex"), "retail xex"u8.ToArray());
        temp.File(TestPath.Combine(layout.Game, @"Media\db\gamedb.slt"), "vanilla db"u8.ToArray());
        temp.File(TestPath.Combine(layout.Game, FakeMod.Tunables), "vanilla tunables"u8.ToArray());
        return layout;
    }

    private static async Task<IReadOnlyList<FileProblem>> Install(FakeMod fake, InstallLayout layout, string mode = "normal",
        bool harderAi = false, IProgress<double>? progress = null, CancellationToken cancellation = default)
    {
        using var mod = ModSource.Open(fake.Root);
        return await Pfp4Installer.InstallAsync(mod, fake.Manifest, layout, mode, harderAi, progress, cancellation);
    }

    private static string Text(string path) => File.ReadAllText(path);

    private static string Packages(InstallLayout layout) => TestPath.Combine(layout.Content, "4D530910", "00000002");

    [Fact]
    public async Task Applies_game_files_and_the_chosen_mode()
    {
        using var temp = new TempDir();
        var fake = new FakeMod(temp);
        var layout = Layout(temp);
        Assert.Empty(await Install(fake, layout, "hard", harderAi: true));
        Assert.Equal("mod xex", Text(TestPath.Combine(layout.Game, "default.xex")));
        Assert.Equal("harder db hard", Text(TestPath.Combine(layout.Game, @"Media\db\gamedb.slt")));
        Assert.Equal("harder tunables hard", Text(TestPath.Combine(layout.Game, FakeMod.Tunables)));
        Assert.Equal("merge hard", Text(TestPath.Combine(Packages(layout), FakeMod.PackageA, FakeMod.Merge)));
    }

    [Fact]
    public async Task Without_harder_ai_the_mods_main_tunables_apply()
    {
        using var temp = new TempDir();
        var fake = new FakeMod(temp);
        var layout = Layout(temp);
        Assert.Empty(await Install(fake, layout, "vanilla"));
        Assert.Equal("db vanilla", Text(TestPath.Combine(layout.Game, @"Media\db\gamedb.slt")));
        Assert.Equal("main tunables", Text(TestPath.Combine(layout.Game, FakeMod.Tunables)));
    }

    [Fact]
    public async Task Caches_every_mode_for_later_switches()
    {
        using var temp = new TempDir();
        var fake = new FakeMod(temp);
        var layout = Layout(temp);
        Assert.Empty(await Install(fake, layout));
        foreach (var mode in FakeMod.Modes)
        {
            var cache = TestPath.Combine(layout.Pfp4Modes, mode);
            Assert.Equal($"db {mode}", Text(TestPath.Combine(cache, "gamedb.slt")));
            Assert.Equal($"harder db {mode}", Text(TestPath.Combine(cache, "harder-ai", "gamedb.slt")));
            Assert.Equal($"harder tunables {mode}", Text(TestPath.Combine(cache, "harder-ai", "GameTunableSettings.ini")));
            Assert.Equal($"merge {mode}", Text(TestPath.Combine(cache, "dlc", FakeMod.PackageA, FakeMod.Merge)));
            Assert.Equal($"save {mode}", Text(TestPath.Combine(cache, "skip-intro", "Car", @"ForzaProfile\ForzaProfile")));
        }
        Assert.Equal("main tunables", Text(TestPath.Combine(layout.Pfp4, "GameTunableSettings.ini")));
    }

    [Fact]
    public async Task Writes_every_package_into_content()
    {
        using var temp = new TempDir();
        var fake = new FakeMod(temp);
        var layout = Layout(temp);
        Assert.Empty(await Install(fake, layout));
        Assert.Equal("offer", Text(TestPath.Combine(Packages(layout), FakeMod.PackageA, @"Media\1.puboffer")));
        Assert.Equal(FakeMod.PatchedTrack, Text(TestPath.Combine(Packages(layout), FakeMod.PackageB, FakeMod.Track)));

        var headers = TestPath.Combine(layout.Content, "4D530910", "Headers", "00000002");
        var a = File.ReadAllBytes(TestPath.Combine(headers, FakeMod.PackageA + ".header"));
        var b = File.ReadAllBytes(TestPath.Combine(headers, FakeMod.PackageB + ".header"));
        Assert.Equal(0x148, a.Length);
        Assert.Equal((1u, 2u), (BinaryPrimitives.ReadUInt32BigEndian(a), BinaryPrimitives.ReadUInt32BigEndian(a.AsSpan(4))));
        Assert.Equal("Car Pack", Encoding.BigEndianUnicode.GetString(a, 8, 0x100).TrimEnd('\0'));
        Assert.Equal(FakeMod.PackageA, Encoding.ASCII.GetString(a, 0x108, 42));
        Assert.Equal(0ul, BinaryPrimitives.ReadUInt64BigEndian(a.AsSpan(0x138)));
        Assert.Equal(0x4D530910u, BinaryPrimitives.ReadUInt32BigEndian(a.AsSpan(0x140)));
        Assert.Equal(a.AsSpan(0x132, 6).ToArray(), new byte[6]);
        Assert.Equal(0x148 + 4, b.Length);
        Assert.Equal(0xFFFFFFFFu, BinaryPrimitives.ReadUInt32LittleEndian(b.AsSpan(0x148)));
        Assert.Contains("Track Pack", Text(TestPath.Combine(layout.Content, "names.toml")));
    }

    [Fact]
    public async Task Refuses_a_changed_mod_file()
    {
        using var temp = new TempDir();
        var fake = new FakeMod(temp);
        var layout = Layout(temp);
        var changed = $@"Disc 2 and DLCs\00000002\{FakeMod.PackageA}\Media\1.puboffer";
        File.WriteAllText(fake.ModFile(changed), "OFFER");
        var problems = await Install(fake, layout);
        Assert.Equal([new FileProblem(changed, FileProblemKind.HashMismatch)], problems);
        Assert.Equal("retail xex", Text(TestPath.Combine(layout.Game, "default.xex")));
        Assert.False(Directory.Exists(layout.Content));
        Assert.False(Directory.Exists(layout.Pfp4));
    }

    [Fact]
    public async Task Never_writes_excluded_files()
    {
        using var temp = new TempDir();
        var fake = new FakeMod(temp);
        var layout = Layout(temp);
        Assert.Empty(await Install(fake, layout));
        Assert.False(Directory.Exists(TestPath.Combine(layout.Game, "Media", "RenderScenarios")));
    }

    [Fact]
    public async Task An_interrupted_install_is_redone()
    {
        using var temp = new TempDir();
        var fake = new FakeMod(temp);
        var once = Layout(temp, TestPath.Combine(temp.Path, "once"));
        Assert.Empty(await Install(fake, once, "sandbox"));

        var twice = Layout(temp, TestPath.Combine(temp.Path, "twice"));
        using var cut = new CancellationTokenSource();
        var reports = 0;
        await Assert.ThrowsAnyAsync<OperationCanceledException>(() =>
            Install(fake, twice, "sandbox", progress: new SyncProgress<double>(_ => { if (++reports == 5) cut.Cancel(); }),
                cancellation: cut.Token));
        Assert.Empty(await Install(fake, twice, "sandbox"));
        Assert.Equal(Tree(once.Root), Tree(twice.Root));
    }

    [Fact]
    public async Task The_mod_chosen_again_over_an_applied_install_restores_its_cache()
    {
        using var temp = new TempDir();
        var fake = new FakeMod(temp);
        var once = Layout(temp, TestPath.Combine(temp.Path, "once"));
        Assert.Empty(await Install(fake, once, "hard"));

        var again = Layout(temp, TestPath.Combine(temp.Path, "again"));
        Assert.Empty(await Install(fake, again, "hard"));
        Directory.Delete(again.Pfp4Modes, recursive: true);
        Assert.Empty(await Install(fake, again, "hard"));
        Assert.Equal(Tree(once.Root), Tree(again.Root));
    }

    private static List<(string, string)> Tree(string root) =>
        Directory.EnumerateFiles(root, "*", SearchOption.AllDirectories)
            .Select(f => (Path.GetRelativePath(root, f), TestDiscs.Sha(File.ReadAllBytes(f))))
            .Order().ToList();

    [Fact]
    public async Task Writes_paths_longer_than_260_characters()
    {
        using var temp = new TempDir();
        var fake = new FakeMod(temp);
        var layout = Layout(temp, TestPath.Combine(temp.Path, new string('r', 120), new string('s', 120), "data"));
        Assert.Empty(await Install(fake, layout));
        var merge = TestPath.Combine(Packages(layout), FakeMod.PackageA, FakeMod.Merge);
        Assert.True(merge.Length > 260);
        Assert.Equal("merge normal", Text(merge));
    }

    [Fact]
    public void The_content_bytes_are_every_file_the_install_reads()
    {
        using var temp = new TempDir();
        var fake = new FakeMod(temp);
        var expected = Directory.EnumerateFiles(fake.Root, "*", SearchOption.AllDirectories)
            .Where(f => !f.Contains("RenderScenarios")).Sum(f => new FileInfo(f).Length);
        Assert.Equal(expected, Pfp4Installer.ContentBytes(fake.Manifest));
    }
}
