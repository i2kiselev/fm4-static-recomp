using Fm4StaticRecomp.Launcher.Core.Install;
using Fm4StaticRecomp.Launcher.Core.Platform;
using Fm4StaticRecomp.Launcher.Core.Profiles;

namespace Fm4StaticRecomp.Launcher.Tests.Profiles;

public class ProfileStoreTests
{
    private static (TempDir Temp, ProfileStore Store, InstallLayout Layout) Make()
    {
        var temp = new TempDir();
        var layout = new InstallLayout(temp.Path);
        Directory.CreateDirectory(layout.Content);
        return (temp, new ProfileStore(layout, Platforms.Current), layout);
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
        var store = new ProfileStore(layout, new NoLinks());
        Assert.Throws<IOException>(() => store.Create("Main"));
        Assert.Empty(store.List());
        Assert.Empty(Directory.GetDirectories(layout.Profiles));
    }

    [Fact]
    public void A_moved_launcher_folder_gets_its_content_link_pointed_at_the_new_content()
    {
        using var temp = new TempDir();
        var old = new InstallLayout(Path.Combine(temp.Path, "old", "data"));
        new ProfileStore(old, Platforms.Current).Create("Main");
        Directory.Move(Path.Combine(temp.Path, "old"), Path.Combine(temp.Path, "new"));
        var layout = new InstallLayout(Path.Combine(temp.Path, "new", "data"));
        File.WriteAllText(Path.Combine(layout.Content, "probe"), "x");
        var store = new ProfileStore(layout, Platforms.Current);
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
        var profile = store.Import(Path.Combine(temp.Path, "old"), "Imported");
        Assert.True(File.Exists(Path.Combine(profile.User, "B13EBABEBABEBABE", "4D530910", "00000001", "ForzaProfile", "ForzaProfile")));
        Assert.False(File.Exists(Path.Combine(profile.User, ProfileStore.ContentFolderName, "4D530910", "00000002", "pack", "big.bin")));
    }

    [Fact]
    public void Import_refuses_a_folder_without_a_save()
    {
        var (temp, store, layout) = Make();
        using var _t = temp;
        temp.File("old/user/0000000000000000/4D530910/00000002/pack/big.bin", [2]);
        temp.File("old/notes.txt", [3]);
        Assert.Throws<IOException>(() => store.Import(Path.Combine(temp.Path, "old"), "Imported"));
        Assert.Empty(store.List());
        Assert.False(Directory.Exists(layout.Profiles));
    }

    [Fact]
    public void Import_refuses_the_install_s_own_folder()
    {
        var (temp, store, layout) = Make();
        using var _t = temp;
        temp.File("B13EBABEBABEBABE/4D530910/00000001/ForzaProfile/ForzaProfile", [1]);
        Assert.Throws<IOException>(() => store.Import(temp.Path, "Imported"));
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
            Assert.ThrowsAny<IOException>(() => store.Import(Path.Combine(temp.Path, "old"), "Imported"));
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
}
