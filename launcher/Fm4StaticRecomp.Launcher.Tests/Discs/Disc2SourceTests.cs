// Copyright (c) 2026, i2kiselev
// BSD 3-Clause License: see LICENSE in the repository root.
using Fm4StaticRecomp.Launcher.Core.Discs;

namespace Fm4StaticRecomp.Launcher.Tests.Discs;

public class Disc2SourceTests
{
    private static Dictionary<string, byte[]> Packages() => Enumerable.Range(1, 4).ToDictionary(
        n => $"4d5309100000000{n}",
        n => (byte[])[.. GodBuilder.Header("LIVE", 2, 0x4D530910, 0, $"Disc 2 part {n}"), (byte)n]);

    private static string[] List(string folder) =>
        Directory.GetFiles(folder, "*", SearchOption.AllDirectories).Order(StringComparer.Ordinal).ToArray();

    [Fact]
    public async Task Verifies_a_package_folder_in_place()
    {
        using var temp = new TempDir();
        var manifest = DiscManifest.Load(TestDiscs.WriteManifest(temp, TestDiscs.Pal(), Packages()));
        foreach (var (name, data) in Packages()) temp.File($"input/00000002/{name}", data);
        var folder = Path.Combine(temp.Path, "input");
        var before = List(folder);
        var input = DiscInput.Detect(folder)!;
        Assert.Equal(DiscInputKind.PackageFolder, input.Kind);
        Assert.Empty(await Disc2Source.VerifyAsync(input, manifest, null, CancellationToken.None));
        Assert.Equal(before, List(folder));
    }

    [Fact]
    public async Task Takes_an_extracted_disc_2_with_its_default_xex_as_its_packages_but_refuses_disc_1()
    {
        using var temp = new TempDir();
        var manifest = DiscManifest.Load(TestDiscs.WriteManifest(temp, TestDiscs.Pal(), Packages()));
        temp.File("disc2/default.xex", [1]);
        temp.File("disc2/media/nxeart.bin", [2]);
        foreach (var (name, data) in Packages()) temp.File($"disc2/Content/0000000000000000/4D530910/00000002/{name}", data);
        foreach (var (name, data) in TestDiscs.Pal()) temp.File("disc1/" + name, data);
        var input = DiscInput.DetectDisc2(Path.Combine(temp.Path, "disc2"))!;
        Assert.Equal(DiscInputKind.PackageFolder, input.Kind);
        Assert.Empty(await Disc2Source.VerifyAsync(input, manifest, null, CancellationToken.None));
        Assert.Null(DiscInput.DetectDisc2(Path.Combine(temp.Path, "disc1")));
        Assert.Equal(DiscInputKind.PackageFolder, DiscInput.DetectDisc2(Path.Combine(temp.Path, "disc2", "Content"))!.Kind);
    }

    [Fact]
    public async Task Verifies_and_stages_from_an_iso()
    {
        using var temp = new TempDir();
        var manifest = DiscManifest.Load(TestDiscs.WriteManifest(temp, TestDiscs.Pal(), Packages()));
        var disc = Packages().ToDictionary(p => $"Content/0000000000000000/4D530910/00000002/{p.Key}", p => p.Value);
        var input = DiscInput.Detect(temp.File("d2.iso", GdfImageBuilder.Build(disc)))!;
        Assert.Equal(DiscInputKind.Iso, input.Kind);
        Assert.Empty(await Disc2Source.VerifyAsync(input, manifest, null, CancellationToken.None));
        var staged = await Disc2Source.StageAsync(input, manifest, Path.Combine(temp.Path, "staging"), CancellationToken.None);
        Assert.Empty(staged.Problems);
        Assert.Equal(Packages()["4d53091000000003"], File.ReadAllBytes(Path.Combine(staged.Folder, "4d53091000000003")));
    }

    [Fact]
    public async Task Reports_a_missing_and_a_changed_package()
    {
        using var temp = new TempDir();
        var manifest = DiscManifest.Load(TestDiscs.WriteManifest(temp, TestDiscs.Pal(), Packages()));
        foreach (var (name, data) in Packages().Where(p => !p.Key.EndsWith('4')))
        {
            if (name.EndsWith('2')) data[^1] ^= 0xFF;
            temp.File($"input/{name}", data);
        }
        var problems = await Disc2Source.VerifyAsync(DiscInput.Detect(Path.Combine(temp.Path, "input"))!, manifest, null,
            CancellationToken.None);
        Assert.Equal([new FileProblem("4d53091000000002", FileProblemKind.HashMismatch),
            new FileProblem("4d53091000000004", FileProblemKind.Missing)], problems.ToArray());
    }

    [Fact]
    public async Task A_staging_with_a_package_missing_is_not_all_of_disc_2()
    {
        using var temp = new TempDir();
        var manifest = DiscManifest.Load(TestDiscs.WriteManifest(temp, TestDiscs.Pal(), Packages()));
        foreach (var (name, data) in Packages().Where(p => !p.Key.EndsWith('4'))) temp.File($"input/{name}", data);
        var input = DiscInput.Detect(Path.Combine(temp.Path, "input"))!;
        var partial = await Disc2Source.StageAsync(input, manifest, Path.Combine(temp.Path, "staging"), CancellationToken.None);
        Assert.False(partial.Complete);
        temp.File("input/4d53091000000004", Packages()["4d53091000000004"]);
        var whole = await Disc2Source.StageAsync(input, manifest, Path.Combine(temp.Path, "staging"), CancellationToken.None);
        Assert.True(whole.Complete);
    }
}
