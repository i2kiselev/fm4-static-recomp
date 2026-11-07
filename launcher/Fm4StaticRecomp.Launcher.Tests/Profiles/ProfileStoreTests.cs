// Copyright (c) 2026, i2kiselev
// BSD 3-Clause License: see LICENSE in the repository root.
using Fm4StaticRecomp.Launcher.Core.Content;
using Fm4StaticRecomp.Launcher.Core.Install;
using Fm4StaticRecomp.Launcher.Core.Platform;
using Fm4StaticRecomp.Launcher.Core.Profiles;
using Fm4StaticRecomp.Launcher.Core.Setup;

namespace Fm4StaticRecomp.Launcher.Tests.Profiles;

public class ProfileStoreTests
{
    private static (TempDir Temp, ProfileStore Store, InstallLayout Layout) Make(string installGame = "vanilla")
    {
        var temp = new TempDir();
        var layout = new InstallLayout(temp.Path);
        Directory.CreateDirectory(layout.Content);
        return (temp, new ProfileStore(layout, Platforms.Current, installGame), layout);
    }

    [Fact]
    public void Creates_a_profile_with_the_content_link()
    {
        var (temp, store, layout) = Make();
        using var _ = temp;
        File.WriteAllText(Path.Combine(layout.Content, "probe"), "x");
        var profile = store.Create("Ilya's Garage");
        Assert.Equal("ilya-s-garage", profile.Id);
        Assert.Equal("Ilya s Garage", profile.Gamertag);
        Assert.True(File.Exists(Path.Combine(profile.User, ProfileStore.ContentFolderName, "probe")));
        Assert.Equal(["ilya-s-garage"], store.List().Select(p => p.Id));
    }

    [Fact]
    public void A_profile_whose_link_fails_is_not_left_behind()
    {
        using var temp = new TempDir();
        var layout = new InstallLayout(temp.Path);
        var store = new ProfileStore(layout, new NoLinks(), "vanilla");
        Assert.Throws<IOException>(() => store.Create("Main"));
        Assert.Empty(store.List());
        Assert.Empty(Directory.GetDirectories(layout.Profiles));
    }

    [Fact]
    public void A_moved_launcher_folder_gets_its_content_link_pointed_at_the_new_content()
    {
        using var temp = new TempDir();
        var old = new InstallLayout(Path.Combine(temp.Path, "old", "data"));
        new ProfileStore(old, Platforms.Current, "vanilla").Create("Main");
        Directory.Move(Path.Combine(temp.Path, "old"), Path.Combine(temp.Path, "new"));
        var layout = new InstallLayout(Path.Combine(temp.Path, "new", "data"));
        File.WriteAllText(Path.Combine(layout.Content, "probe"), "x");
        var store = new ProfileStore(layout, Platforms.Current, "vanilla");
        var profile = store.List().Single();
        Assert.False(File.Exists(Path.Combine(profile.User, ProfileStore.ContentFolderName, "probe")));
        store.EnsureContentLink(profile);
        Assert.True(File.Exists(Path.Combine(profile.User, ProfileStore.ContentFolderName, "probe")));
        store.EnsureContentLink(profile);
        Assert.True(File.Exists(Path.Combine(profile.User, ProfileStore.ContentFolderName, "probe")));
    }

    [Fact]
    public void A_copied_launcher_folder_gets_its_empty_content_folder_replaced_by_the_link()
    {
        var (temp, store, layout) = Make();
        using var _ = temp;
        File.WriteAllText(Path.Combine(layout.Content, "probe"), "x");
        var profile = store.Create("Main");
        var link = Path.Combine(profile.User, ProfileStore.ContentFolderName);
        Directory.Delete(link);
        Directory.CreateDirectory(link);
        store.EnsureContentLink(profile);
        Assert.True(Platforms.Current.IsDirectoryLink(link));
        Assert.True(File.Exists(Path.Combine(link, "probe")));
    }

    [Fact]
    public void A_content_folder_holding_files_is_left_alone()
    {
        var (temp, store, _) = Make();
        using var _t = temp;
        var profile = store.Create("Main");
        var link = Path.Combine(profile.User, ProfileStore.ContentFolderName);
        Directory.Delete(link);
        temp.File(Path.Combine(link, "4D530910", "00000002", "pack", "big.bin"), [2]);
        store.EnsureContentLink(profile);
        Assert.False(Platforms.Current.IsDirectoryLink(link));
        Assert.True(File.Exists(Path.Combine(link, "4D530910", "00000002", "pack", "big.bin")));
    }

    private sealed class NoLinks : IPlatform
    {
        public bool SupportsDxgiPresentation => false;
        public string ExecutableName(string baseName) => baseName;
        public void CreateDirectoryLink(string link, string target) => throw new IOException("The drive holds no links.");
        public bool IsDirectoryLink(string path) => false;
        public System.Text.Encoding ConsoleEncoding => System.Text.Encoding.UTF8;
    }

    [Fact]
    public void Ids_stay_unique_and_fixed_on_rename()
    {
        var (temp, store, _) = Make();
        using var _t = temp;
        var a = store.Create("Main");
        var b = store.Create("Main");
        Assert.NotEqual(a.Id, b.Id);
        var renamed = store.Rename(a.Id, "Career", "Racer1");
        Assert.Equal(a.Id, renamed.Id);
        Assert.Equal("Career", store.List().Single(p => p.Id == a.Id).Name);
    }

    [Fact]
    public void Delete_moves_the_profile_aside()
    {
        var (temp, store, layout) = Make();
        using var _ = temp;
        var profile = store.Create("Old");
        store.Delete(profile.Id);
        Assert.Empty(store.List());
        Assert.Single(Directory.GetDirectories(layout.Deleted));
        Assert.True(Directory.Exists(layout.Content));
    }

    [Fact]
    public void Import_copies_saves_but_not_installed_content()
    {
        var (temp, store, _) = Make();
        using var _t = temp;
        temp.File("old/user/B13EBABEBABEBABE/4D530910/00000001/ForzaProfile/ForzaProfile", [1]);
        temp.File("old/user/0000000000000000/4D530910/00000002/pack/big.bin", [2]);
        var profile = store.Import(Path.Combine(temp.Path, "old"), "Imported", "vanilla");
        Assert.True(File.Exists(Path.Combine(profile.User, "B13EBABEBABEBABE", "4D530910", "00000001", "ForzaProfile", "ForzaProfile")));
        Assert.False(File.Exists(Path.Combine(profile.User, ProfileStore.ContentFolderName, "4D530910", "00000002", "pack", "big.bin")));
        Assert.Null(profile.Xuid);
    }

    // Xenia Canary's header of a save: the package's container header, its display name at 0x411.
    private static byte[] CanaryHeader(string displayName)
    {
        var header = new byte[40960];
        System.Text.Encoding.ASCII.GetBytes("CON ", header);
        System.Text.Encoding.BigEndianUnicode.GetBytes(displayName, header.AsSpan(0x411));
        return header;
    }

    [Fact]
    public void Import_of_xenia_s_content_folder_keeps_the_save_s_xuid_and_rewrites_its_headers()
    {
        var (temp, store, _) = Make();
        using var _t = temp;
        temp.File(@"content\E030000047B76145\4D530910\00000001\ForzaProfile\ForzaProfile", [1]);
        temp.File(@"content\E030000047B76145\4D530910\Headers\00000001\ForzaProfile.header", CanaryHeader("Wesx1"));
        temp.File(@"content\E030000047B76145\FFFE07D1\00010000\E030000047B76145\Account", [2]);
        temp.File(@"content\0000000000000000\4D530910\00000002\pack\big.bin", [3]);
        temp.File(@"content\E0300000AAAAAAAA\535107DB\00000001\Other\save", [4]);
        var profile = store.Import(Path.Combine(temp.Path, "content"), "Xenia", "vanilla");
        Assert.Equal("E030000047B76145", profile.Xuid);
        store.Rename(profile.Id, "Renamed", "Racer");
        Assert.Equal("E030000047B76145", store.Get(profile.Id).Xuid);
        var title = Path.Combine(profile.User, "E030000047B76145", "4D530910");
        Assert.True(File.Exists(Path.Combine(title, "00000001", "ForzaProfile", "ForzaProfile")));
        Assert.Equal(ContentHeader.Build(ContentHeader.SaveGame, "Wesx1", "ForzaProfile", null),
            File.ReadAllBytes(Path.Combine(title, "Headers", "00000001", "ForzaProfile.header")));
        Assert.False(File.Exists(Path.Combine(profile.User, ProfileStore.ContentFolderName, "4D530910", "00000002", "pack", "big.bin")));
        Assert.False(Directory.Exists(Path.Combine(profile.User, "E0300000AAAAAAAA")));
    }

    [Fact]
    public void Import_of_one_xenia_profile_s_folder_takes_that_profile()
    {
        var (temp, store, _) = Make();
        using var _t = temp;
        temp.File(@"content\E030000047B76145\4D530910\00000001\ForzaProfile\ForzaProfile", [1]);
        temp.File(@"content\E030000047B76145\4D530910\Headers\00000001\ForzaProfile.header", CanaryHeader("Wesx1"));
        var profile = store.Import(Path.Combine(temp.Path, "content", "E030000047B76145") + Path.DirectorySeparatorChar, "Xenia", "vanilla");
        Assert.Equal("E030000047B76145", profile.Xuid);
        var title = Path.Combine(profile.User, "E030000047B76145", "4D530910");
        Assert.True(File.Exists(Path.Combine(title, "00000001", "ForzaProfile", "ForzaProfile")));
        Assert.Equal(ContentHeader.Build(ContentHeader.SaveGame, "Wesx1", "ForzaProfile", null),
            File.ReadAllBytes(Path.Combine(title, "Headers", "00000001", "ForzaProfile.header")));
    }

    // A profile imported from Xenia before 0.3.1 holds the Xenia save the game never read beside the
    // career the game made under the runtime's XUID: it moves whole, signing in as its stamp says.
    [Theory]
    [InlineData(null)]
    [InlineData("E030000047B76145")]
    public void Import_of_our_profile_holding_two_xuids_takes_both_and_the_stamp_s_xuid(string? stampedXuid)
    {
        var (temp, store, _) = Make();
        using var _t = temp;
        temp.File(@"old\profile.toml", System.Text.Encoding.UTF8.GetBytes(
            "name = \"Old\"\ngamertag = \"Old\"\ngame = \"vanilla\"\n" + (stampedXuid is null ? "" : $"xuid = \"{stampedXuid}\"\n")));
        temp.File(@"old\user\B13EBABEBABEBABE\4D530910\00000001\ForzaProfile\ForzaProfile", [1]);
        temp.File(@"old\user\E030000047B76145\4D530910\00000001\ForzaProfile\ForzaProfile", [2]);
        var profile = store.Import(Path.Combine(temp.Path, "old"), "Moved", "pfp4");
        Assert.Equal(stampedXuid, profile.Xuid);
        Assert.True(File.Exists(Path.Combine(profile.User, "B13EBABEBABEBABE", "4D530910", "00000001", "ForzaProfile", "ForzaProfile")));
        Assert.True(File.Exists(Path.Combine(profile.User, "E030000047B76145", "4D530910", "00000001", "ForzaProfile", "ForzaProfile")));
    }

    // A profile 0.3.0 imported from Xenia: the Xenia folder copied as it was, no xuid recorded.
    private static void OldXeniaImport(TempDir temp, bool withRuntimeCareer)
    {
        temp.File(@"profiles\old\profile.toml", "name = \"Old\"\ngamertag = \"Old\"\n"u8.ToArray());
        temp.File(@"profiles\old\user\E030000047B76145\4D530910\00000001\ForzaProfile\ForzaProfile", [1]);
        temp.File(@"profiles\old\user\E030000047B76145\4D530910\Headers\00000001\ForzaProfile.header", CanaryHeader("Wesx1"));
        if (withRuntimeCareer) temp.File(@"profiles\old\user\B13EBABEBABEBABE\4D530910\00000001\ForzaProfile\ForzaProfile", [2]);
    }

    [Fact]
    public void A_xenia_save_imported_before_0_3_1_is_signed_in_with_once_repaired()
    {
        var (temp, store, _) = Make();
        using var _t = temp;
        OldXeniaImport(temp, withRuntimeCareer: false);
        Assert.Single(store.RepairXeniaImports());
        var repaired = Assert.Single(store.List());
        Assert.Equal(("old", "E030000047B76145"), (repaired.Id, repaired.Xuid));
        Assert.Equal(ContentHeader.Build(ContentHeader.SaveGame, "Wesx1", "ForzaProfile", null),
            File.ReadAllBytes(Path.Combine(repaired.User, "E030000047B76145", "4D530910", "Headers", "00000001", "ForzaProfile.header")));
        Assert.Empty(store.RepairXeniaImports());
    }

    [Fact]
    public void A_xenia_save_imported_before_0_3_1_beside_a_new_career_gets_a_profile_of_its_own()
    {
        var (temp, store, _) = Make();
        using var _t = temp;
        OldXeniaImport(temp, withRuntimeCareer: true);
        Assert.Single(store.RepairXeniaImports());
        Assert.Null(store.Get("old").Xuid);
        var xenia = store.List().Single(p => p.Id != "old");
        Assert.Equal(("Old (Xenia)", "E030000047B76145"), (xenia.Name, xenia.Xuid));
        Assert.True(File.Exists(Path.Combine(xenia.User, "E030000047B76145", "4D530910", "00000001", "ForzaProfile", "ForzaProfile")));
        Assert.False(Directory.Exists(Path.Combine(xenia.User, ProfileStore.SaveXuid)));
        Assert.Equal(ContentHeader.Build(ContentHeader.SaveGame, "Wesx1", "ForzaProfile", null),
            File.ReadAllBytes(Path.Combine(xenia.User, "E030000047B76145", "4D530910", "Headers", "00000001", "ForzaProfile.header")));
        Assert.Empty(store.RepairXeniaImports());
        Assert.Equal(2, store.List().Count);
    }

    [Fact]
    public void The_profile_keeping_its_new_career_is_left_as_it_was()
    {
        var (temp, store, _) = Make();
        using var _t = temp;
        OldXeniaImport(temp, withRuntimeCareer: true);
        var header = TestPath.Combine(temp.Path, @"profiles\old\user\E030000047B76145\4D530910\Headers\00000001\ForzaProfile.header");
        store.RepairXeniaImports();
        Assert.Equal(CanaryHeader("Wesx1"), File.ReadAllBytes(header));
        Assert.Equal([2], File.ReadAllBytes(TestPath.Combine(temp.Path, @"profiles\old\user\B13EBABEBABEBABE\4D530910\00000001\ForzaProfile\ForzaProfile")));
    }

    [Fact]
    public void Several_xenia_saves_each_get_a_profile_named_by_their_xuid()
    {
        var (temp, store, _) = Make();
        using var _t = temp;
        OldXeniaImport(temp, withRuntimeCareer: false);
        temp.File(@"profiles\old\user\e0300000aaaaaaaa\4D530910\00000001\ForzaProfile\ForzaProfile", [3]);
        Assert.Equal(2, store.RepairXeniaImports().Count);
        Assert.Equal(["Old", "Old (Xenia E030000047B76145)", "Old (Xenia e0300000aaaaaaaa)"],
            store.List().Select(p => p.Name).Order(StringComparer.Ordinal));
        Assert.Null(store.Get("old").Xuid);
        Assert.Empty(store.RepairXeniaImports());
    }

    [Fact]
    public void A_copy_cut_short_is_made_again_and_a_finished_one_is_kept()
    {
        var (temp, store, _) = Make();
        using var _t = temp;
        OldXeniaImport(temp, withRuntimeCareer: true);
        temp.File(@"profiles\old\user\E0300000AAAAAAAA\4D530910\00000001\ForzaProfile\ForzaProfile", [3]);
        temp.File(@"profiles\done\profile.toml", "name = \"Done\"\nxenia_copy_of = \"old/E030000047B76145\"\nxuid = \"E030000047B76145\"\n"u8.ToArray());
        temp.File(@"profiles\cut\profile.toml", "name = \"Cut\"\nxenia_copy_of = \"old/E0300000AAAAAAAA\"\n"u8.ToArray());
        temp.File(@"profiles\cut\user\E0300000AAAAAAAA\4D530910\00000001\ForzaProfile\Forza", [3]);
        Directory.CreateDirectory(TestPath.Combine(temp.Path, @"profiles\cut\user", ProfileStore.ContentFolderName));
        Assert.Single(store.RepairXeniaImports());
        Assert.Equal(["Done", "Old", "Old (Xenia E0300000AAAAAAAA)"], store.List().Select(p => p.Name).Order(StringComparer.Ordinal));
        Assert.Equal("E0300000AAAAAAAA", store.List().Single(p => p.Name.EndsWith("AAAA)")).Xuid);
    }

    [Fact]
    public void An_unreadable_profile_is_reported_and_the_others_are_repaired()
    {
        var (temp, store, _) = Make();
        using var _t = temp;
        OldXeniaImport(temp, withRuntimeCareer: false);
        temp.File(@"profiles\broken\profile.toml", "name = \"Broken\"\n"u8.ToArray());
        var gone = Path.Combine(temp.Path, "gone");
        Directory.CreateDirectory(gone);
        Platforms.Current.CreateDirectoryLink(TestPath.Combine(temp.Path, @"profiles\broken\user"), gone);
        Directory.Delete(gone);
        var lines = store.RepairXeniaImports();
        Assert.Equal(2, lines.Count);
        Assert.Contains(lines, l => l.StartsWith("Profile Broken:") && l.Contains("could not be repaired"));
        Assert.Equal("E030000047B76145", store.Get("old").Xuid);
    }

    [Fact]
    public void An_import_of_a_repaired_profile_is_not_split_again()
    {
        var (temp, store, _) = Make();
        using var _t = temp;
        temp.File(@"elsewhere\profile.toml", "name = \"Old\"\ngame = \"vanilla\"\nxenia_repaired = true\n"u8.ToArray());
        temp.File(@"elsewhere\user\B13EBABEBABEBABE\4D530910\00000001\ForzaProfile\ForzaProfile", [2]);
        temp.File(@"elsewhere\user\E030000047B76145\4D530910\00000001\ForzaProfile\ForzaProfile", [1]);
        store.Import(Path.Combine(temp.Path, "elsewhere"), "Moved", "vanilla");
        Assert.Empty(store.RepairXeniaImports());
    }

    [Fact]
    public void Repair_leaves_profiles_of_the_runtime_s_own_or_with_a_recorded_xuid_alone()
    {
        var (temp, store, _) = Make();
        using var _t = temp;
        temp.File(@"profiles\own\profile.toml", "name = \"Own\"\n"u8.ToArray());
        temp.File(@"profiles\own\user\b13ebabebabebabe\4D530910\00000001\ForzaProfile\ForzaProfile", [1]);
        temp.File(@"profiles\new\profile.toml", "name = \"New\"\nxuid = \"E030000047B76145\"\n"u8.ToArray());
        temp.File(@"profiles\new\user\E030000047B76145\4D530910\00000001\ForzaProfile\ForzaProfile", [1]);
        Assert.Empty(store.RepairXeniaImports());
        Assert.Equal(2, store.List().Count);
    }

    [Fact]
    public void Only_a_save_s_container_header_is_rewritten()
    {
        var live = CanaryHeader("Pack");
        System.Text.Encoding.ASCII.GetBytes("LIVE", live);
        Assert.Null(ContentHeader.FromContainer(live, ContentHeader.Marketplace, "pack"));
        Assert.Null(ContentHeader.FromContainer([0x43, 0x4F, 0x4E, 0x20], ContentHeader.SaveGame, "ForzaProfile"));
        Assert.NotNull(ContentHeader.FromContainer(CanaryHeader("Wesx1"), ContentHeader.SaveGame, "ForzaProfile"));
    }

    [Fact]
    public void Import_refuses_a_folder_with_saves_of_several_profiles()
    {
        var (temp, store, layout) = Make();
        using var _t = temp;
        temp.File(@"content\E030000047B76145\4D530910\00000001\ForzaProfile\ForzaProfile", [1]);
        temp.File(@"content\E03000004331734B\4D530910\00000001\ForzaProfile\ForzaProfile", [1]);
        var error = Assert.Throws<IOException>(() => store.Import(Path.Combine(temp.Path, "content"), "Xenia", "vanilla"));
        Assert.Contains("E03000004331734B", error.Message);
        Assert.False(Directory.Exists(layout.Profiles));
    }

    [Fact]
    public void Import_keeps_a_header_already_in_the_runtime_s_layout()
    {
        var (temp, store, _) = Make();
        using var _t = temp;
        var own = ContentHeader.Build(ContentHeader.SaveGame, "User", "ForzaProfile", null);
        temp.File(@"content\E030000047B76145\4D530910\00000001\ForzaProfile\ForzaProfile", [1]);
        temp.File(@"content\E030000047B76145\4D530910\Headers\00000001\ForzaProfile.header", own);
        var profile = store.Import(Path.Combine(temp.Path, "content"), "Xenia", "vanilla");
        Assert.Equal(own, File.ReadAllBytes(Path.Combine(profile.User, "E030000047B76145", "4D530910", "Headers", "00000001", "ForzaProfile.header")));
    }

    [Fact]
    public void Import_refuses_a_folder_without_a_save()
    {
        var (temp, store, layout) = Make();
        using var _t = temp;
        temp.File("old/user/0000000000000000/4D530910/00000002/pack/big.bin", [2]);
        temp.File("old/notes.txt", [3]);
        Assert.Throws<IOException>(() => store.Import(Path.Combine(temp.Path, "old"), "Imported", "vanilla"));
        Assert.Empty(store.List());
        Assert.False(Directory.Exists(layout.Profiles));
    }

    [Fact]
    public void Import_refuses_the_install_s_own_folder()
    {
        var (temp, store, layout) = Make();
        using var _t = temp;
        temp.File("B13EBABEBABEBABE/4D530910/00000001/ForzaProfile/ForzaProfile", [1]);
        Assert.Throws<IOException>(() => store.Import(temp.Path, "Imported", "vanilla"));
        Assert.Empty(store.List());
        Assert.False(Directory.Exists(layout.Profiles));
    }

    [Fact]
    public void An_import_that_fails_midway_leaves_no_profile()
    {
        var (temp, store, layout) = Make();
        using var _t = temp;
        File.WriteAllText(Path.Combine(layout.Content, "probe"), "x");
        temp.File("old/user/B13EBABEBABEBABE/4D530910/00000001/ForzaProfile/ForzaProfile", [1]);
        var locked = temp.File("old/user/B13EBABEBABEBABE/4D530910/00000001/ForzaProfile/Locked", [2]);
        using (new FileStream(locked, FileMode.Open, FileAccess.Read, FileShare.None))
            Assert.ThrowsAny<IOException>(() => store.Import(Path.Combine(temp.Path, "old"), "Imported", "vanilla"));
        Assert.Empty(store.List());
        Assert.Empty(Directory.GetDirectories(layout.Profiles));
        Assert.True(File.Exists(Path.Combine(layout.Content, "probe")));
    }

    [Fact]
    public void A_name_of_symbols_still_makes_a_profile()
    {
        var (temp, store, _) = Make();
        using var _t = temp;
        var profile = store.Create("!!!");
        Assert.Equal("profile", profile.Id);
        Assert.Equal("Player", profile.Gamertag);
        Assert.Equal("profile-2", store.Create("???").Id);
    }

    [Theory]
    [InlineData("Racer", true)]
    [InlineData("Racer 2", true)]
    [InlineData("2Fast", false)]
    [InlineData("", false)]
    [InlineData("SixteenCharsLong", false)]
    [InlineData("Ünïcode", false)]
    public void Validates_gamertags(string gamertag, bool valid) =>
        Assert.Equal(valid, ProfileStore.IsValidGamertag(gamertag));

    [Fact]
    public void A_cyrillic_name_keeps_its_name_and_gets_a_player_gamertag()
    {
        var (temp, store, _) = Make();
        using var _t = temp;
        var profile = store.Create("Илья");
        Assert.Equal(("Илья", "profile", "Player"), (profile.Name, profile.Id, profile.Gamertag));
        Assert.Equal("Илья", store.List().Single().Name);
    }

    private static ProfileStore Pfp4Store(InstallLayout layout, string mode = "normal", bool harderAi = false)
    {
        var state = SetupState.Load(layout.SetupState);
        (state.Game, state.Pfp4Mode, state.Pfp4HarderAi, state.Pfp4Applied) = ("pfp4", mode, harderAi, true);
        state.Save();
        return new ProfileStore(layout, Platforms.Current, "pfp4");
    }

    private static void Save(TempDir temp, string user) =>
        temp.File(Path.Combine(user, "B13EBABEBABEBABE", "4D530910", "00000001", "ForzaProfile", "ForzaProfile"), [1]);

    [Fact]
    public void A_profile_without_game_is_vanilla()
    {
        var (temp, store, layout) = Make();
        using var _t = temp;
        temp.File(Path.Combine(layout.Profiles, "old", "profile.toml"), "name = \"Old\"\ngamertag = \"Old\"\n"u8.ToArray());
        var old = store.Get("old");
        Assert.Equal(("vanilla", (string?)null, (bool?)null), (old.Game, old.Pfp4Mode, old.Pfp4HarderAi));
    }

    [Fact]
    public void Create_stamps_the_install_game_and_mode()
    {
        var (temp, store, layout) = Make();
        using var _t = temp;
        var plain = store.Create("Plain");
        Assert.Equal(("vanilla", (string?)null), (plain.Game, plain.Pfp4Mode));
        var created = Pfp4Store(layout, "hard", true).Create("Modded");
        Assert.Equal(("pfp4", "hard", (bool?)true), (created.Game, created.Pfp4Mode, created.Pfp4HarderAi));
        var text = File.ReadAllText(Path.Combine(created.Folder, "profile.toml"));
        Assert.Contains("game = \"pfp4\"", text);
        Assert.Contains("pfp4_mode = \"hard\"", text);
    }

    [Fact]
    public void Import_refuses_a_vanilla_save_in_a_pfp4_install()
    {
        var (temp, _, layout) = Make();
        using var _t = temp;
        var store = Pfp4Store(layout);
        Save(temp, Path.Combine("old", "user"));
        var error = Assert.Throws<IOException>(() => store.Import(Path.Combine(temp.Path, "old"), "Imported", "vanilla"));
        Assert.Contains("Project Forza Plus 4", error.Message);
        Assert.Empty(store.List());
    }

    [Fact]
    public void Import_refuses_a_stamped_profile_of_the_other_game_whatever_was_declared()
    {
        var (temp, _, layout) = Make();
        using var _t = temp;
        var other = new InstallLayout(Path.Combine(temp.Path, "other"));
        var source = new ProfileStore(other, Platforms.Current, "vanilla").Create("Vanilla career");
        Save(temp, source.User);
        var pfp4 = Pfp4Store(layout);
        Assert.Contains("vanilla saves", Assert.Throws<IOException>(() => pfp4.Import(source.Folder, "Imported", "pfp4")).Message);
        Assert.Contains("vanilla saves", Assert.Throws<IOException>(() => pfp4.Import(source.User, "Imported", "pfp4")).Message);
        Assert.Empty(pfp4.List());
    }

    [Fact]
    public void Import_carries_the_stamped_mode()
    {
        var (temp, _, layout) = Make();
        using var _t = temp;
        var source = Pfp4Store(new InstallLayout(Path.Combine(temp.Path, "other")), "sandbox").Create("Sandbox career");
        Save(temp, source.User);
        var imported = Pfp4Store(layout, "normal").Import(source.Folder, "Copy", "vanilla");
        Assert.Equal(("pfp4", "sandbox", (bool?)false), (imported.Game, imported.Pfp4Mode, imported.Pfp4HarderAi));
    }

    [Fact]
    public void Import_without_stamp_leaves_the_mode_unknown()
    {
        var (temp, _, layout) = Make();
        using var _t = temp;
        var store = Pfp4Store(layout, "hard");
        temp.File(@"xenia\E03000004331734B\4D530910\00000001\ForzaProfile\ForzaProfile", [1]);
        var imported = store.Import(Path.Combine(temp.Path, "xenia"), "Xenia", "pfp4");
        Assert.Equal(("pfp4", (string?)null, (bool?)null), (imported.Game, imported.Pfp4Mode, imported.Pfp4HarderAi));
        Assert.Null(imported.ImportWarning);
    }

    [Fact]
    public void An_import_past_a_warning_keeps_the_warning()
    {
        var (temp, _, layout) = Make();
        using var _t = temp;
        var store = Pfp4Store(layout, "normal");
        temp.File(@"xenia\E03000004331734B\4D530910\00000001\ForzaProfile\ForzaProfile", [1]);
        var imported = store.Import(Path.Combine(temp.Path, "xenia"), "Xenia", "pfp4", "the DLC folder beside the saves is vanilla FM4's");
        Assert.Equal("the DLC folder beside the saves is vanilla FM4's", store.Get(imported.Id).ImportWarning);
        store.Rename(imported.Id, "Renamed", "Racer");
        Assert.Equal("the DLC folder beside the saves is vanilla FM4's", store.Get(imported.Id).ImportWarning);
    }

    [Fact]
    public void A_skip_intro_profile_holds_the_save_and_its_mode()
    {
        var (temp, _, layout) = Make();
        using var _t = temp;
        var store = Pfp4Store(layout, "vanilla");
        var save = Path.Combine(layout.Pfp4Modes, "vanilla", "skip-intro", "Fox");
        temp.File(Path.Combine(save, "ForzaProfile", "ForzaProfile"), [7]);
        temp.File(Path.Combine(save, "ForzaProfile", "Liveries", "6.liv"), [8]);
        var profile = store.CreateFromSkipIntro("Fox start", save);
        var title = Path.Combine(profile.User, "B13EBABEBABEBABE", "4D530910");
        Assert.Equal([7], File.ReadAllBytes(Path.Combine(title, "00000001", "ForzaProfile", "ForzaProfile")));
        Assert.Equal([8], File.ReadAllBytes(Path.Combine(title, "00000001", "ForzaProfile", "Liveries", "6.liv")));
        var header = File.ReadAllBytes(Path.Combine(title, "Headers", "00000001", "ForzaProfile.header"));
        Assert.Equal(0x148, header.Length);
        Assert.Equal(1u, System.Buffers.Binary.BinaryPrimitives.ReadUInt32BigEndian(header.AsSpan(4)));
        Assert.Equal("User", System.Text.Encoding.BigEndianUnicode.GetString(header, 8, 0x100).TrimEnd('\0'));
        Assert.Equal("ForzaProfile", System.Text.Encoding.ASCII.GetString(header, 0x108, 42).TrimEnd('\0'));
        Assert.Equal(("pfp4", "vanilla", (bool?)false), (profile.Game, profile.Pfp4Mode, profile.Pfp4HarderAi));
        Assert.True(Directory.Exists(Path.Combine(profile.User, ProfileStore.ContentFolderName)));
    }

    [Fact]
    public void A_skip_intro_save_missing_from_the_cache_asks_for_the_mod_and_creates_nothing()
    {
        var (temp, _, layout) = Make();
        using var _t = temp;
        var store = Pfp4Store(layout, "vanilla");
        var error = Assert.Throws<InvalidDataException>(() =>
            store.CreateFromSkipIntro("Fox start", Path.Combine(layout.Pfp4Modes, "vanilla", "skip-intro", "Fox")));
        Assert.Contains("CHOOSE THE MOD AGAIN", error.Message);
        Assert.Empty(store.List());
    }

    [Fact]
    public void Mode_warning_names_the_profiles_mode()
    {
        var profile = new Profile("a", "a", "A", "A", null, "pfp4", "hard", false);
        Assert.Contains("Hard", ProfileStore.ModeWarning(profile, "normal")!);
        Assert.Null(ProfileStore.ModeWarning(profile, "hard"));
    }

    [Fact]
    public void Harder_ai_alone_warns_of_nothing()
    {
        Assert.Null(ProfileStore.ModeWarning(new Profile("a", "a", "A", "A", null, "pfp4", "hard", true), "hard"));
    }

    [Fact]
    public void No_warning_for_an_unknown_mode()
    {
        Assert.Null(ProfileStore.ModeWarning(new Profile("a", "a", "A", "A", null, "pfp4", null, null), "normal"));
    }

    [Fact]
    public void Recording_the_mode_ends_the_warning()
    {
        var (temp, _, layout) = Make();
        using var _t = temp;
        var store = Pfp4Store(layout, "hard");
        var profile = store.Create("Career");
        store.RecordMode(profile.Id, "normal", true);
        var read = store.Get(profile.Id);
        Assert.Equal(("normal", (bool?)true), (read.Pfp4Mode, read.Pfp4HarderAi));
        Assert.Null(ProfileStore.ModeWarning(read, "normal"));
        store.Rename(profile.Id, "Renamed", "Racer");
        Assert.Equal(("pfp4", "normal"), (store.Get(profile.Id).Game, store.Get(profile.Id).Pfp4Mode));
    }
}
