// Copyright (c) 2026, i2kiselev
// BSD 3-Clause License: see LICENSE in the repository root.
using Fm4StaticRecomp.Launcher.Core.Install;
using Fm4StaticRecomp.Launcher.Core.Pfp4;
using Fm4StaticRecomp.Launcher.Core.Setup;

namespace Fm4StaticRecomp.Launcher.Tests.Pfp4;

public class Pfp4ModesTests
{
    private sealed record Install(FakeMod Mod, InstallLayout Layout, SetupState State)
    {
        public string GameDb => File.ReadAllText(TestPath.Combine(Layout.Game, @"Media\db\gamedb.slt"));
        public string Tunables => File.ReadAllText(TestPath.Combine(Layout.Game, FakeMod.Tunables));
        public string Merge => File.ReadAllText(TestPath.Combine(Layout.Content, "4D530910", "00000002", FakeMod.PackageA, FakeMod.Merge));
        public void Switch(string mode, bool harderAi) => Pfp4Modes.Switch(Layout, Mod.Manifest, State, mode, harderAi);
    }

    private static async Task<Install> Installed(TempDir temp, string mode, bool harderAi)
    {
        var fake = new FakeMod(temp);
        var layout = new InstallLayout(TestPath.Combine(temp.Path, "data"));
        temp.File(TestPath.Combine(layout.Game, "default.xex"), "retail xex"u8.ToArray());
        using (var mod = ModSource.Open(fake.Root))
            Assert.Empty(await Pfp4Installer.InstallAsync(mod, fake.Manifest, layout, mode, harderAi, null, CancellationToken.None));
        var state = SetupState.Load(layout.SetupState);
        (state.Game, state.Pfp4Mode, state.Pfp4HarderAi, state.Pfp4Applied) = ("pfp4", mode, harderAi, true);
        state.Save();
        return new Install(fake, layout, state);
    }

    [Fact]
    public async Task Switching_mode_replaces_the_game_db_and_dlc_dbs()
    {
        using var temp = new TempDir();
        var install = await Installed(temp, "normal", false);
        install.Switch("hard", false);
        Assert.Equal(("db hard", "merge hard", "main tunables"), (install.GameDb, install.Merge, install.Tunables));
        var saved = SetupState.Load(install.Layout.SetupState);
        Assert.Equal(("hard", false), (saved.Pfp4Mode, saved.Pfp4HarderAi));
    }

    [Fact]
    public async Task Harder_ai_off_restores_the_main_tunables()
    {
        using var temp = new TempDir();
        var install = await Installed(temp, "normal", true);
        Assert.Equal("harder tunables normal", install.Tunables);
        install.Switch("normal", false);
        Assert.Equal(("db normal", "main tunables"), (install.GameDb, install.Tunables));
        install.Switch("vanilla", true);
        Assert.Equal(("harder db vanilla", "harder tunables vanilla"), (install.GameDb, install.Tunables));
        Assert.True(SetupState.Load(install.Layout.SetupState).Pfp4HarderAi);
    }

    [Fact]
    public async Task A_missing_cache_stops_and_keeps_the_current_mode()
    {
        using var temp = new TempDir();
        var install = await Installed(temp, "normal", false);
        Directory.Delete(install.Layout.Pfp4Modes, recursive: true);
        var error = Assert.Throws<InvalidDataException>(() => install.Switch("hard", true));
        Assert.Contains("CHOOSE THE MOD AGAIN", error.Message);
        Assert.Equal(("db normal", "merge normal", "main tunables"), (install.GameDb, install.Merge, install.Tunables));
        var saved = SetupState.Load(install.Layout.SetupState);
        Assert.Equal(("normal", false), (saved.Pfp4Mode, saved.Pfp4HarderAi));
    }

    [Fact]
    public async Task A_damaged_cache_file_stops_and_keeps_the_current_mode()
    {
        using var temp = new TempDir();
        var install = await Installed(temp, "normal", false);
        File.WriteAllText(TestPath.Combine(install.Layout.Pfp4Modes, "hard", "dlc", FakeMod.PackageA, FakeMod.Merge), "merge HARD");
        var error = Assert.Throws<InvalidDataException>(() => install.Switch("hard", false));
        Assert.Contains("mod", error.Message);
        Assert.Equal(("db normal", "merge normal"), (install.GameDb, install.Merge));
        Assert.Equal("normal", SetupState.Load(install.Layout.SetupState).Pfp4Mode);
    }

    [Fact]
    public async Task A_switch_that_fails_while_copying_keeps_the_current_mode()
    {
        using var temp = new TempDir();
        var install = await Installed(temp, "normal", false);
        Directory.CreateDirectory(TestPath.Combine(install.Layout.Content, "4D530910", "00000002", FakeMod.PackageA, FakeMod.Merge + ".new"));
        Assert.True(Record.Exception(() => install.Switch("hard", false)) is IOException or UnauthorizedAccessException);
        Assert.Equal(("db normal", "merge normal", "main tunables"), (install.GameDb, install.Merge, install.Tunables));
        Assert.False(File.Exists(TestPath.Combine(install.Layout.Game, @"Media\db\gamedb.slt.new")));
        Assert.Equal("normal", SetupState.Load(install.Layout.SetupState).Pfp4Mode);
    }

    [Fact]
    public async Task A_switch_never_writes_through_a_linked_game_file()
    {
        using var temp = new TempDir();
        var install = await Installed(temp, "normal", false);
        var db = TestPath.Combine(install.Layout.Game, @"Media\db\gamedb.slt");
        var other = TestPath.Combine(temp.Path, "other-install-gamedb.slt");
        File.Move(db, other);
        TestPath.CreateHardLink(db, other);
        install.Switch("hard", false);
        Assert.Equal("db hard", install.GameDb);
        Assert.Equal("db normal", File.ReadAllText(other));
    }

}
