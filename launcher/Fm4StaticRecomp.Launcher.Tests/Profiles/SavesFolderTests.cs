// Copyright (c) 2026, i2kiselev
// BSD 3-Clause License: see LICENSE in the repository root.
using Fm4StaticRecomp.Launcher.Core.Install;
using Fm4StaticRecomp.Launcher.Core.Platform;
using Fm4StaticRecomp.Launcher.Core.Profiles;

namespace Fm4StaticRecomp.Launcher.Tests.Profiles;

public class SavesFolderTests
{
    private static (TempDir Temp, InstallLayout Layout, ProfileStore Store) Make(string installGame = "vanilla")
    {
        var temp = new TempDir();
        var layout = new InstallLayout(Path.Combine(temp.Path, "launcher", "data"));
        Directory.CreateDirectory(layout.Content);
        File.WriteAllText(Path.Combine(layout.Content, "probe"), "x");
        return (temp, layout, new ProfileStore(layout, Platforms.Current, installGame));
    }

    // Another install's saves folder (an old launcher's, or one synced from another PC).
    private static (InstallLayout Layout, ProfileStore Store) Other(TempDir temp, string game = "vanilla")
    {
        var other = new InstallLayout(Path.Combine(temp.Path, "other", "data")) { SavesRoot = Path.Combine(temp.Path, "FM4 Saves") };
        Directory.CreateDirectory(other.Content);
        return (other, new ProfileStore(other, Platforms.Current, game));
    }

    private static void Save(Profile profile, string text)
    {
        var title = Path.Combine(profile.User, "E00000000000ABCD", "4D530910");
        Directory.CreateDirectory(title);
        File.WriteAllText(Path.Combine(title, "save.bin"), text);
    }

    private static string SaveText(Profile profile) =>
        File.ReadAllText(Path.Combine(profile.User, "E00000000000ABCD", "4D530910", "save.bin"));

    [Fact]
    public void An_empty_new_or_used_saves_folder_outside_data_is_accepted()
    {
        var (temp, layout, _) = Make();
        using var _t = temp;
        Assert.Null(SavesFolder.Problem(layout, Path.Combine(temp.Path, "new saves")));
        var (other, store) = Other(temp);
        store.Create("Main");
        Assert.Null(SavesFolder.Problem(layout, other.SavesRoot));
    }

    [Fact]
    public void A_folder_inside_data_is_refused_but_data_itself_is_the_default()
    {
        var (temp, layout, _) = Make();
        using var _t = temp;
        Assert.Contains("inside the launcher's data folder", SavesFolder.Problem(layout, Path.Combine(layout.Root, "mine")));
        var moved = layout with { SavesRoot = Path.Combine(temp.Path, "saves") };
        Assert.Null(SavesFolder.Problem(moved, layout.Root));
    }

    [Fact]
    public void A_folder_inside_the_current_saves_folder_is_refused()
    {
        var (temp, layout, _) = Make();
        using var _t = temp;
        var moved = layout with { SavesRoot = Path.Combine(temp.Path, "saves") };
        Assert.Contains("the current saves folder, or inside it", SavesFolder.Problem(moved, Path.Combine(moved.Profiles, "x")));
    }

    [Fact]
    public void A_profiles_folder_itself_is_refused_and_the_folder_above_named()
    {
        var (temp, layout, _) = Make();
        using var _t = temp;
        var (other, store) = Other(temp);
        store.Create("Main");
        var problem = SavesFolder.Problem(layout, other.Profiles);
        Assert.Contains("profiles folder itself", problem);
        Assert.Contains(other.SavesRoot, problem);
    }

    [Fact]
    public void Saves_of_the_other_game_are_named_for_a_warning()
    {
        var (temp, _, _) = Make();
        using var _t = temp;
        var (other, store) = Other(temp);
        store.Create("Vanilla career");
        // A profile of 0.3.0 or older carries no game: it is a vanilla one.
        temp.File(Path.Combine(other.Profiles, "old", "profile.toml"), "name = \"Old\"\n"u8.ToArray());
        new ProfileStore(other, Platforms.Current, ProfileStore.Pfp4).Create("Mod career");
        Assert.Equal(["Old", "Vanilla career"], SavesFolder.OtherGameProfiles(other.SavesRoot, ProfileStore.Pfp4));
        Assert.Equal(["Mod career"], SavesFolder.OtherGameProfiles(other.SavesRoot, "vanilla"));
        Assert.Empty(SavesFolder.OtherGameProfiles(Path.Combine(temp.Path, "nothing here"), "vanilla"));
        var warning = SavesFolder.OtherGameWarning(ProfileStore.Pfp4, ["Old", "Vanilla career"]);
        Assert.Contains("vanilla", warning);
        Assert.Contains("not guaranteed", warning);
        Assert.Contains("fresh career", warning);
    }

    [Fact]
    public void Moves_profiles_backups_and_deleted_profiles_and_links_the_content_again()
    {
        var (temp, layout, store) = Make();
        using var _t = temp;
        var main = store.Create("Main");
        Save(main, "career");
        store.Delete(store.Create("Old").Id);
        Directory.CreateDirectory(layout.Backups);
        File.WriteAllText(Path.Combine(layout.Backups, "main-0.3.0-20261031-120000.zip"), "zip");
        var moved = layout with { SavesRoot = Path.Combine(temp.Path, "FM4 Saves") };

        var move = SavesFolder.Copy(layout, moved, Platforms.Current);
        var profile = new ProfileStore(moved, Platforms.Current, "vanilla").List().Single();
        Assert.Equal("career", SaveText(profile));
        Assert.True(File.Exists(Path.Combine(profile.User, ProfileStore.ContentFolderName, "probe")));
        Assert.True(File.Exists(Path.Combine(moved.Backups, "main-0.3.0-20261031-120000.zip")));
        Assert.Single(Directory.GetDirectories(moved.Deleted));
        Assert.True(Directory.Exists(layout.Profiles));

        SavesFolder.Remove(move);
        Assert.False(Directory.Exists(layout.Profiles));
        Assert.False(Directory.Exists(layout.Backups));
        Assert.False(Directory.Exists(layout.Deleted));
        Assert.True(File.Exists(Path.Combine(layout.Content, "probe")));
        Assert.True(File.Exists(Path.Combine(profile.User, ProfileStore.ContentFolderName, "probe")));
    }

    [Fact]
    public void Merges_into_a_folder_with_saves_keeping_both_careers_of_one_name()
    {
        var (temp, layout, store) = Make();
        using var _t = temp;
        var (other, otherStore) = Other(temp);
        Save(otherStore.Create("Main"), "theirs");
        Directory.CreateDirectory(other.Backups);
        File.WriteAllText(Path.Combine(other.Backups, "main-0.3.0-20261030-120000.zip"), "their zip");
        Save(store.Create("Main"), "mine");
        Directory.CreateDirectory(layout.Backups);
        File.WriteAllText(Path.Combine(layout.Backups, "main-0.3.0-20261031-120000.zip"), "my zip");
        var moved = layout with { SavesRoot = other.SavesRoot };

        SavesFolder.Copy(layout, moved, Platforms.Current);
        var profiles = new ProfileStore(moved, Platforms.Current, "vanilla").List();
        Assert.Equal([("main", "Main", "theirs"), ("main-2", "Main (2)", "mine")],
            profiles.Select(p => (p.Id, p.Name, SaveText(p))).OrderBy(p => p.Id));
        Assert.All(profiles, p => Assert.True(File.Exists(Path.Combine(p.User, ProfileStore.ContentFolderName, "probe"))));
        Assert.Equal("their zip", File.ReadAllText(Path.Combine(moved.Backups, "main-0.3.0-20261030-120000.zip")));
        Assert.Equal("my zip", File.ReadAllText(Path.Combine(moved.Backups, "main-2-0.3.0-20261031-120000.zip")));
    }

    [Fact]
    public void A_profile_without_a_save_goes_to_deleted_when_the_folder_has_one_of_its_name()
    {
        var (temp, layout, store) = Make();
        using var _t = temp;
        var (other, otherStore) = Other(temp);
        Save(otherStore.Create("Main"), "theirs");
        store.Create("Main");
        var moved = layout with { SavesRoot = other.SavesRoot };

        SavesFolder.Copy(layout, moved, Platforms.Current);
        Assert.Equal("theirs", SaveText(new ProfileStore(moved, Platforms.Current, "vanilla").List().Single()));
        Assert.Single(Directory.GetDirectories(moved.Deleted));
    }

    [Fact]
    public void Profiles_of_the_other_game_keep_the_warning_for_their_logs()
    {
        var (temp, layout, _) = Make(ProfileStore.Pfp4);
        using var _t = temp;
        var (other, otherStore) = Other(temp);
        otherStore.Create("Vanilla career");
        new ProfileStore(other, Platforms.Current, ProfileStore.Pfp4).Create("Mod career");
        SavesFolder.NoteOtherGame(other.SavesRoot, ProfileStore.Pfp4, "taken over past a warning");
        var profiles = new ProfileStore(other, Platforms.Current, ProfileStore.Pfp4).List();
        Assert.Equal("taken over past a warning", profiles.Single(p => p.Name == "Vanilla career").ImportWarning);
        Assert.Null(profiles.Single(p => p.Name == "Mod career").ImportWarning);
    }

    [Fact]
    public void A_drive_without_links_is_refused_before_anything_is_copied()
    {
        var (temp, layout, store) = Make();
        using var _t = temp;
        store.Create("Main");
        var moved = layout with { SavesRoot = Path.Combine(temp.Path, "usb") };
        var error = Assert.Throws<IOException>(() => SavesFolder.Copy(layout, moved, new NoLinks()));
        Assert.Contains("cannot hold", error.Message);
        Assert.False(Directory.Exists(moved.Profiles));
        Assert.Single(store.List());
    }

    [Fact]
    public void A_failed_copy_takes_back_only_what_it_added()
    {
        var (temp, layout, store) = Make();
        using var _t = temp;
        var (other, otherStore) = Other(temp);
        Save(otherStore.Create("Theirs"), "theirs");
        Save(store.Create("Mine"), "mine");
        store.Delete(store.Create("Gone").Id);
        var locked = Path.Combine(Directory.GetDirectories(layout.Deleted).Single(), "profile.toml");
        var moved = layout with { SavesRoot = other.SavesRoot };
        using (new FileStream(locked, FileMode.Open, FileAccess.Read, FileShare.None))
            Assert.ThrowsAny<IOException>(() => SavesFolder.Copy(layout, moved, Platforms.Current));
        Assert.Equal(["Theirs"], new ProfileStore(moved, Platforms.Current, "vanilla").List().Select(p => p.Name));
        Assert.False(Directory.Exists(moved.Deleted) && Directory.EnumerateFileSystemEntries(moved.Deleted).Any());
        Assert.Equal(["Mine"], store.List().Select(p => p.Name));
    }

    // Review finding 1: the same folder reached by another path would be merged into itself and deleted.
    [Fact]
    public void The_current_saves_folder_reached_through_a_junction_is_refused()
    {
        var (temp, layout, store) = Make();
        using var _t = temp;
        var moved = layout with { SavesRoot = Path.Combine(temp.Path, "saves") };
        Directory.CreateDirectory(moved.Backups);
        var alias = Path.Combine(temp.Path, "alias");
        Platforms.Current.CreateDirectoryLink(alias, moved.SavesRoot);
        Assert.Contains("the current saves folder", SavesFolder.Problem(moved, alias));
        Assert.Contains("the current saves folder", SavesFolder.Problem(moved, Path.Combine(alias, "backups", "x")));
        // The data folder by another path while the saves are at the default.
        var dataAlias = Path.Combine(temp.Path, "data alias");
        Platforms.Current.CreateDirectoryLink(dataAlias, layout.Root);
        Assert.Contains("the current saves folder", SavesFolder.Problem(layout, dataAlias));
        Assert.Empty(Directory.GetFiles(moved.SavesRoot));
    }

    // Review finding 2: its saves would live inside the other install, and both launchers would share them.
    [Fact]
    public void Another_install_s_data_folder_is_refused()
    {
        var (temp, layout, _) = Make();
        using var _t = temp;
        var other = new InstallLayout(Path.Combine(temp.Path, "old launcher", "data"));
        temp.File(Path.Combine(other.Config, "launcher.toml"), "active_profile = \"main\"\n"u8.ToArray());
        foreach (var folder in new[] { other.Root, Path.Combine(other.Root, "mine") })
            Assert.Contains("PROFILE → IMPORT SAVE", SavesFolder.Problem(layout, folder));
    }

    [Fact]
    public void An_empty_profiles_folder_is_refused_too()
    {
        var (temp, layout, _) = Make();
        using var _t = temp;
        Directory.CreateDirectory(Path.Combine(temp.Path, "FM4 Saves", "profiles"));
        Assert.Contains("profiles folder itself", SavesFolder.Problem(layout, Path.Combine(temp.Path, "FM4 Saves", "profiles")));
    }

    // Review finding 5: the active profile follows its career when it is renamed.
    [Fact]
    public void The_copy_reports_the_profiles_it_renamed()
    {
        var (temp, layout, store) = Make();
        using var _t = temp;
        var (other, otherStore) = Other(temp);
        Save(otherStore.Create("Main"), "theirs");
        Save(store.Create("Main"), "mine");
        var move = SavesFolder.Copy(layout, layout with { SavesRoot = other.SavesRoot }, Platforms.Current);
        Assert.Equal("main-2", move.Renamed["main"]);
    }

    // Review finding 11: a new id is free in the folder and among the profiles still to come.
    [Fact]
    public void A_renamed_profile_never_takes_the_id_of_another_local_profile()
    {
        var (temp, layout, store) = Make();
        using var _t = temp;
        var (other, otherStore) = Other(temp);
        Save(otherStore.Create("Main"), "theirs");
        Save(store.Create("Main"), "mine");
        Save(store.Create("Main"), "my second");
        var moved = layout with { SavesRoot = other.SavesRoot };
        SavesFolder.Copy(layout, moved, Platforms.Current);
        Assert.Equal([("main", "theirs"), ("main-2", "my second"), ("main-3", "mine")],
            new ProfileStore(moved, Platforms.Current, "vanilla").List().Select(p => (p.Id, SaveText(p))).OrderBy(p => p.Id));
    }

    // Review finding 6: a save kept elsewhere through a link of its own stays linked, and Remove leaves its target.
    [Fact]
    public void A_link_inside_a_profile_moves_as_a_link()
    {
        var (temp, layout, store) = Make();
        using var _t = temp;
        var main = store.Create("Main");
        var cloud = Path.Combine(temp.Path, "cloud", "E00000000000ABCD");
        temp.File(Path.Combine(cloud, "4D530910", "save.bin"), "in the cloud"u8.ToArray());
        Platforms.Current.CreateDirectoryLink(Path.Combine(main.User, "E00000000000ABCD"), cloud);
        var moved = layout with { SavesRoot = Path.Combine(temp.Path, "saves") };
        var move = SavesFolder.Copy(layout, moved, Platforms.Current);
        SavesFolder.Remove(move);
        var profile = new ProfileStore(moved, Platforms.Current, "vanilla").List().Single();
        Assert.Equal("in the cloud", SaveText(profile));
        Assert.True(File.Exists(Path.Combine(cloud, "4D530910", "save.bin")));
    }

    // Review finding 4c: copies left behind by a failed Remove are no longer profiles.
    [Fact]
    public void A_failed_remove_leaves_no_profiles_behind()
    {
        // Unix permits unlinking a file held open by another process.
        if (!OperatingSystem.IsWindows()) return;
        var (temp, layout, store) = Make();
        using var _t = temp;
        var main = store.Create("Main");
        Save(main, "career");
        var move = SavesFolder.Copy(layout, layout with { SavesRoot = Path.Combine(temp.Path, "saves") }, Platforms.Current);
        using (new FileStream(Path.Combine(main.User, "E00000000000ABCD", "4D530910", "save.bin"), FileMode.Open, FileAccess.Read, FileShare.None))
            Assert.ThrowsAny<IOException>(() => SavesFolder.Remove(move));
        Assert.Empty(store.List());
    }

    // Review finding 4a: an entry cut short by a closed window is never left under its real name.
    [Fact]
    public void Nothing_is_left_beside_the_saves_while_copying()
    {
        var (temp, layout, store) = Make();
        using var _t = temp;
        Save(store.Create("Main"), "career");
        var moved = layout with { SavesRoot = Path.Combine(temp.Path, "saves") };
        SavesFolder.Copy(layout, moved, Platforms.Current);
        Assert.Equal(["profiles"], Directory.GetFileSystemEntries(moved.SavesRoot).Select(Path.GetFileName));
    }

    // Review finding 10: a link that is made but does not lead to the content is no use to the game.
    [Fact]
    public void A_link_that_does_not_reach_the_content_is_refused()
    {
        var (temp, layout, store) = Make();
        using var _t = temp;
        store.Create("Main");
        var moved = layout with { SavesRoot = Path.Combine(temp.Path, "saves") };
        Assert.Contains("cannot hold", Assert.Throws<IOException>(() => SavesFolder.Copy(layout, moved, new EmptyFolderLinks())).Message);
        Assert.Equal(["probe"], Directory.GetFiles(layout.Content).Select(Path.GetFileName));
    }

    // Review finding 7: deleting a tree never walks into a link's target.
    [Fact]
    public void Remove_leaves_what_a_link_in_the_old_saves_points_at()
    {
        var (temp, layout, store) = Make();
        using var _t = temp;
        store.Create("Main");
        var elsewhere = Path.Combine(temp.Path, "elsewhere");
        temp.File(Path.Combine(elsewhere, "inner", "keep.txt"), "x"u8.ToArray());
        Directory.CreateDirectory(layout.Deleted);
        Platforms.Current.CreateDirectoryLink(Path.Combine(layout.Deleted, "linked"), elsewhere);
        var move = SavesFolder.Copy(layout, layout with { SavesRoot = Path.Combine(temp.Path, "saves") }, Platforms.Current);
        SavesFolder.Remove(move);
        Assert.True(File.Exists(Path.Combine(elsewhere, "inner", "keep.txt")));
        Assert.False(Directory.Exists(layout.Deleted));
    }

    // A link made as a plain folder: created, but nothing of the target shows through it.
    private sealed class EmptyFolderLinks : IPlatform
    {
        public bool SupportsDxgiPresentation => false;
        public string ExecutableName(string baseName) => baseName;
        public void CreateDirectoryLink(string link, string target) => Directory.CreateDirectory(link);
        public bool IsDirectoryLink(string path) => false;
        public System.Text.Encoding ConsoleEncoding => System.Text.Encoding.UTF8;
    }

    private sealed class NoLinks : IPlatform
    {
        public bool SupportsDxgiPresentation => false;
        public string ExecutableName(string baseName) => baseName;
        public void CreateDirectoryLink(string link, string target) => throw new IOException("The drive holds no links.");
        public bool IsDirectoryLink(string path) => false;
        public System.Text.Encoding ConsoleEncoding => System.Text.Encoding.UTF8;
    }
}
