using Fm4StaticRecomp.Launcher.Core.Discs;

namespace Fm4StaticRecomp.Launcher.Tests.Discs;

public class Disc1StepTests
{
    private static Task<bool> Accept(IReadOnlyList<FileProblem> _) => Task.FromResult(true);

    private static Task<bool> Never(IReadOnlyList<FileProblem> _) => throw new InvalidOperationException("asked");

    private sealed class Fixture : IDisposable
    {
        public TempDir Temp { get; } = new();
        public DiscManifest Manifest { get; }
        public string Game { get; }

        public Fixture()
        {
            Manifest = DiscManifest.Load(TestDiscs.WriteManifest(Temp, TestDiscs.Pal()));
            Game = Path.Combine(Temp.Path, "install", "game");
        }

        public DiscInput Iso(IReadOnlyDictionary<string, byte[]> files) =>
            DiscInput.Detect(Temp.File("d1.iso", GdfImageBuilder.Build(files)))!;

        public void Dispose() => Temp.Dispose();
    }

    private static Dictionary<string, byte[]> PalWith(string path, byte[]? data)
    {
        var files = TestDiscs.Pal();
        if (data is null) files.Remove(path);
        else files[path] = data;
        return files;
    }

    [Fact]
    public async Task Installs_a_pal_iso_and_leaves_no_partial_folder()
    {
        using var f = new Fixture();
        var outcome = await Disc1Step.InstallAsync(f.Iso(TestDiscs.Pal()), f.Manifest, f.Game, Never, null, CancellationToken.None);
        Assert.Equal(new Disc1Outcome.Installed(TestDiscs.PalName, 0), outcome);
        Assert.Equal(TestDiscs.Pal()["default.xex"], File.ReadAllBytes(Path.Combine(f.Game, "default.xex")));
        Assert.Equal([5, 6], File.ReadAllBytes(Path.Combine(f.Game, "Media", "a.bin")));
        Assert.False(Directory.Exists(f.Game + ".partial"));
    }

    [Fact]
    public async Task Installs_from_an_extracted_folder()
    {
        using var f = new Fixture();
        foreach (var (name, data) in TestDiscs.Pal()) f.Temp.File("extracted/" + name, data);
        var input = DiscInput.Detect(Path.Combine(f.Temp.Path, "extracted"))!;
        var outcome = await Disc1Step.InstallAsync(input, f.Manifest, f.Game, Never, null, CancellationToken.None);
        Assert.Equal(new Disc1Outcome.Installed(TestDiscs.PalName, 0), outcome);
        Assert.Equal([8], File.ReadAllBytes(Path.Combine(f.Game, "$SystemUpdate", "su.bin")));
    }

    [Fact]
    public async Task A_missing_optional_file_passes()
    {
        using var f = new Fixture();
        var input = f.Iso(PalWith("$SystemUpdate/su.bin", null));
        var outcome = await Disc1Step.InstallAsync(input, f.Manifest, f.Game, Never, null, CancellationToken.None);
        Assert.Equal(new Disc1Outcome.Installed(TestDiscs.PalName, 0), outcome);
    }

    [Fact]
    public async Task Refuses_a_changed_executable_and_names_both_hashes()
    {
        using var f = new Fixture();
        var changed = TestDiscs.Xex(TestDiscs.PalMediaId, extra: 3);
        var outcome = await Disc1Step.InstallAsync(f.Iso(PalWith("default.xex", changed)), f.Manifest, f.Game, Never, null,
            CancellationToken.None);
        var check = Assert.IsType<Disc1Outcome.Refused>(outcome).Check;
        Assert.Equal(TestDiscs.Sha(changed), check.FoundSha256);
        Assert.Equal(TestDiscs.Sha(TestDiscs.Xex(TestDiscs.PalMediaId)), check.ExpectedSha256);
        Assert.Equal(TestDiscs.PalName, check.Release!.Name);
        Assert.False(check.Matches);
        Assert.False(Directory.Exists(f.Game));
    }

    [Fact]
    public async Task Refuses_an_unknown_media_id()
    {
        using var f = new Fixture();
        var input = f.Iso(PalWith("default.xex", TestDiscs.Xex(0x12345678)));
        var outcome = await Disc1Step.InstallAsync(input, f.Manifest, f.Game, Never, null, CancellationToken.None);
        var check = Assert.IsType<Disc1Outcome.Refused>(outcome).Check;
        Assert.Null(check.Release);
        Assert.Equal("12345678", check.MediaId);
        Assert.Null(check.ExpectedSha256);
    }

    [Fact]
    public async Task Takes_the_media_id_from_the_god_header()
    {
        using var f = new Fixture();
        var god = Path.Combine(f.Temp.Path, "god");
        GodBuilder.Write(god, GdfImageBuilder.Build(TestDiscs.Pal()), mediaId: TestDiscs.UsaMediaId);
        var input = DiscInput.Detect(god)!;
        var outcome = await Disc1Step.InstallAsync(input, f.Manifest, f.Game, Never, null, CancellationToken.None);
        var check = Assert.IsType<Disc1Outcome.Refused>(outcome).Check;
        Assert.Equal(TestDiscs.UsaName, check.Release!.Name);
        Assert.Equal("26FF1101", check.MediaId);
    }

    [Fact]
    public async Task Installs_rev_1_with_a_folder_renamed_by_case()
    {
        using var f = new Fixture();
        var outcome = await Disc1Step.InstallAsync(f.Iso(TestDiscs.Rev1Europe()), f.Manifest, f.Game, Never, null,
            CancellationToken.None);
        Assert.Equal(new Disc1Outcome.Installed(TestDiscs.Rev1EuropeName, 0), outcome);
        Assert.Equal([9], File.ReadAllBytes(Path.Combine(f.Game, "Media", "patch.bin")));
    }

    [Fact]
    public async Task A_1_0_disc_does_not_need_rev_1_files()
    {
        using var f = new Fixture();
        var outcome = await Disc1Step.InstallAsync(f.Iso(TestDiscs.Pal()), f.Manifest, f.Game, Never, null, CancellationToken.None);
        Assert.Equal(new Disc1Outcome.Installed(TestDiscs.PalName, 0), outcome);
        Assert.False(File.Exists(Path.Combine(f.Game, "Media", "patch.bin")));
    }

    [Fact]
    public async Task Takes_rev_1_usa_from_the_god_header()
    {
        using var f = new Fixture();
        var files = TestDiscs.Rev1Europe();
        files["default.xex"] = TestDiscs.Xex(TestDiscs.Rev1UsaMediaId);
        var god = Path.Combine(f.Temp.Path, "god");
        GodBuilder.Write(god, GdfImageBuilder.Build(files), mediaId: TestDiscs.Rev1UsaMediaId);
        var outcome = await Disc1Step.InstallAsync(DiscInput.Detect(god)!, f.Manifest, f.Game, Never, null, CancellationToken.None);
        Assert.Equal(new Disc1Outcome.Installed(TestDiscs.Rev1UsaName, 0), outcome);
    }

    [Fact]
    public async Task Refuses_a_rev_1_executable_on_a_1_0_media_id()
    {
        using var f = new Fixture();
        var god = Path.Combine(f.Temp.Path, "god");
        GodBuilder.Write(god, GdfImageBuilder.Build(TestDiscs.Rev1Europe()), mediaId: TestDiscs.PalMediaId);
        var outcome = await Disc1Step.InstallAsync(DiscInput.Detect(god)!, f.Manifest, f.Game, Never, null, CancellationToken.None);
        var check = Assert.IsType<Disc1Outcome.Refused>(outcome).Check;
        Assert.Equal(TestDiscs.PalName, check.Release!.Name);
        Assert.False(check.Matches);
    }

    [Fact]
    public async Task Installs_a_god_dump()
    {
        using var f = new Fixture();
        var god = Path.Combine(f.Temp.Path, "god");
        GodBuilder.Write(god, GdfImageBuilder.Build(TestDiscs.Pal()));
        var outcome = await Disc1Step.InstallAsync(DiscInput.Detect(god)!, f.Manifest, f.Game, Never, null, CancellationToken.None);
        Assert.Equal(new Disc1Outcome.Installed(TestDiscs.PalName, 0), outcome);
    }

    [Theory]
    [InlineData(true)]
    [InlineData(false)]
    public async Task Lists_other_mismatches_and_installs_when_accepted(bool accept)
    {
        using var f = new Fixture();
        var files = PalWith("Media/a.bin", [5, 0]);
        files.Remove("Media/b.bin");
        IReadOnlyList<FileProblem>? seen = null;
        var outcome = await Disc1Step.InstallAsync(f.Iso(files), f.Manifest, f.Game,
            problems => { seen = problems; return Task.FromResult(accept); }, null, CancellationToken.None);
        Assert.Equal([new FileProblem("Media/a.bin", FileProblemKind.HashMismatch), new FileProblem("Media/b.bin", FileProblemKind.Missing)],
            seen!.ToArray());
        if (accept)
        {
            Assert.Equal(new Disc1Outcome.Installed(TestDiscs.PalName, 2), outcome);
            Assert.True(File.Exists(Path.Combine(f.Game, "default.xex")));
        }
        else
        {
            Assert.IsType<Disc1Outcome.Declined>(outcome);
            Assert.False(Directory.Exists(f.Game));
        }
        Assert.False(Directory.Exists(f.Game + ".partial"));
    }

    [Fact]
    public async Task A_cancelled_extraction_leaves_no_game_folder()
    {
        using var f = new Fixture();
        using var cancel = new CancellationTokenSource();
        var progress = new SyncProgress<double>(_ => cancel.Cancel());
        await Assert.ThrowsAnyAsync<OperationCanceledException>(() =>
            Disc1Step.InstallAsync(f.Iso(TestDiscs.Pal()), f.Manifest, f.Game, Accept, progress, cancel.Token));
        Assert.False(Directory.Exists(f.Game));
        Assert.False(Directory.Exists(f.Game + ".partial"));
    }

    [Fact]
    public async Task A_locked_file_in_the_old_game_never_leaves_it_half_deleted()
    {
        using var f = new Fixture();
        var a = f.Temp.File(@"install\game\a.txt", [1]);
        var b = f.Temp.File(@"install\game\b.txt", [2]);
        Disc1Outcome? outcome = null;
        using (new FileStream(a, FileMode.Open, FileAccess.Read, FileShare.None))
        {
            try { outcome = await Disc1Step.InstallAsync(f.Iso(TestDiscs.Pal()), f.Manifest, f.Game, Never, null, CancellationToken.None); }
            catch (IOException) { }
            catch (UnauthorizedAccessException) { }
        }
        if (outcome is Disc1Outcome.Installed) Assert.True(File.Exists(Path.Combine(f.Game, "default.xex")));
        else Assert.True(File.Exists(a) && File.Exists(b), "the old game lost files");
        Assert.False(Directory.Exists(f.Game + ".partial"));
    }

    [Fact]
    public void Recovers_a_replacement_cut_short()
    {
        using var f = new Fixture();
        f.Temp.File(@"install\game.old\default.xex", [1]);
        Disc1Step.RecoverInterruptedReplace(f.Game);
        Assert.True(File.Exists(Path.Combine(f.Game, "default.xex")));
        Assert.False(Directory.Exists(f.Game + ".old"));
        f.Temp.File(@"install\game.old\stale.bin", [1]);
        Disc1Step.RecoverInterruptedReplace(f.Game);
        Assert.False(Directory.Exists(f.Game + ".old"));
        Assert.True(File.Exists(Path.Combine(f.Game, "default.xex")));
    }

    [Fact]
    public async Task Replaces_an_older_game_folder_only_when_done()
    {
        using var f = new Fixture();
        var old = f.Temp.File(@"install\game\old.txt", [1]);
        var files = PalWith("Media/a.bin", [5, 0]);
        var declined = await Disc1Step.InstallAsync(f.Iso(files), f.Manifest, f.Game, _ => Task.FromResult(false), null,
            CancellationToken.None);
        Assert.IsType<Disc1Outcome.Declined>(declined);
        Assert.True(File.Exists(old));
        var installed = await Disc1Step.InstallAsync(f.Iso(TestDiscs.Pal()), f.Manifest, f.Game, Never, null, CancellationToken.None);
        Assert.IsType<Disc1Outcome.Installed>(installed);
        Assert.False(File.Exists(old));
        Assert.True(File.Exists(Path.Combine(f.Game, "default.xex")));
    }
}
