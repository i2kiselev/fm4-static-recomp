// Copyright (c) 2026, i2kiselev
// BSD 3-Clause License: see LICENSE in the repository root.
using System.Runtime.InteropServices;
using System.Text;
using Fm4StaticRecomp.Launcher.Core.Discs;
using Fm4StaticRecomp.Launcher.Core.Install;
using Fm4StaticRecomp.Launcher.Core.Platform;
using Fm4StaticRecomp.Launcher.Core.Profiles;
using Fm4StaticRecomp.Launcher.Tests.Discs;

namespace Fm4StaticRecomp.Launcher.Tests.Platform;

public class SourcePathTests
{
    private const string Xuid = "E000183BF391C696";

    [Fact]
    public void Disc_shortcuts_resolve_to_iso_and_extracted_folder()
    {
        if (OperatingSystem.IsWindows()) return;
        using var temp = new TempDir();
        var iso = temp.File("disc.iso", GdfImageBuilder.Build(TestDiscs.Pal()));
        var isoLink = temp.File("disc alias.iso");
        File.CreateSymbolicLink(isoLink, iso);
        Assert.Equal(DiscInputKind.Iso, DiscInput.Detect(isoLink)!.Kind);
        temp.File("disc/default.xex", TestDiscs.Xex(TestDiscs.PalMediaId));
        var folderLink = Path.Combine(temp.Path, "disc folder alias");
        Directory.CreateSymbolicLink(folderLink, Path.Combine(temp.Path, "disc"));
        var input = DiscInput.Detect(folderLink)!;
        Assert.Equal(DiscInputKind.ExtractedFolder, input.Kind);
        using var files = input.OpenFiles();
        Assert.NotNull(files.Find("default.xex"));
    }

    [Fact]
    public void Save_shortcut_uses_target_xuid_and_makes_an_independent_copy()
    {
        if (OperatingSystem.IsWindows()) return;
        using var temp = new TempDir();
        var original = temp.File($"original/{Xuid}/4D530910/00000001/ForzaProfile/career", [1, 2, 3]);
        var link = Path.Combine(temp.Path, "Imported save alias");
        Directory.CreateSymbolicLink(link, Path.Combine(temp.Path, "original", Xuid));
        var layout = new InstallLayout(Path.Combine(temp.Path, "install"));
        var profile = new ProfileStore(layout, Platforms.Current, "vanilla").Import(link, "My career", "vanilla");
        Assert.Equal(Xuid, profile.Xuid);
        var copied = Path.Combine(profile.User, Xuid, "4D530910", "00000001", "ForzaProfile", "career");
        Assert.Equal([1, 2, 3], File.ReadAllBytes(copied));
        File.WriteAllBytes(copied, [4]);
        Assert.Equal([1, 2, 3], File.ReadAllBytes(original));
    }

    [Fact]
    public void Shared_user_folder_lists_each_save_but_excludes_content_and_duplicates()
    {
        if (OperatingSystem.IsWindows()) return;
        using var temp = new TempDir();
        temp.File($"user/{Xuid}/4D530910/save", [1]);
        temp.File("user/B13EBABEBABEBABE/4D530910/save", [2]);
        temp.File("user/0000000000000000/4D530910/pack", [3]);
        Directory.CreateSymbolicLink(Path.Combine(temp.Path, "user", "Imported save alias"), Path.Combine(temp.Path, "user", Xuid));
        Assert.Equal(["B13EBABEBABEBABE", Xuid], ProfileStore.ImportCandidates(Path.Combine(temp.Path, "user")).Select(Path.GetFileName));
    }

    [Fact]
    public void Finder_aliases_work_for_disc_files_folders_and_imported_saves()
    {
        if (!OperatingSystem.IsMacOS()) return;
        using var temp = new TempDir();
        var iso = temp.File("disc.iso", GdfImageBuilder.Build(TestDiscs.Pal()));
        var isoAlias = temp.File("Disc image shortcut");
        WriteFinderAlias(iso, isoAlias);
        Assert.Equal(DiscInputKind.Iso, DiscInput.Detect(isoAlias)!.Kind);
        temp.File("disc/default.xex", TestDiscs.Xex(TestDiscs.PalMediaId));
        var folderAlias = temp.File("Extracted disc shortcut");
        WriteFinderAlias(Path.Combine(temp.Path, "disc"), folderAlias);
        Assert.Equal(DiscInputKind.ExtractedFolder, DiscInput.Detect(folderAlias)!.Kind);
        temp.File($"original/{Xuid}/4D530910/00000001/ForzaProfile/career", [1, 2, 3]);
        var saveAlias = temp.File("Existing career shortcut");
        WriteFinderAlias(Path.Combine(temp.Path, "original", Xuid), saveAlias);
        Assert.Equal(Xuid, Path.GetFileName(Assert.Single(ProfileStore.ImportCandidates(saveAlias))));
        var aliasContainer = Path.Combine(temp.Path, "Saved aliases");
        Directory.CreateDirectory(aliasContainer);
        WriteFinderAlias(Path.Combine(temp.Path, "original", Xuid), Path.Combine(aliasContainer, "My imported career"));
        Assert.Equal(Xuid, Path.GetFileName(Assert.Single(ProfileStore.ImportCandidates(aliasContainer))));
        var profile = new ProfileStore(new InstallLayout(Path.Combine(temp.Path, "install")), Platforms.Current, "vanilla")
            .Import(saveAlias, "Imported", "vanilla");
        Assert.Equal(Xuid, profile.Xuid);
        Assert.Equal([1, 2, 3], File.ReadAllBytes(Path.Combine(profile.User, Xuid, "4D530910", "00000001", "ForzaProfile", "career")));
    }

    private const string Framework = "/System/Library/Frameworks/CoreFoundation.framework/CoreFoundation";
    private static void WriteFinderAlias(string target, string alias)
    {
        nint Url(string path) {
            var bytes = Encoding.UTF8.GetBytes(path);
            return CFURLCreateFromFileSystemRepresentation(0, bytes, bytes.Length, 0);
        }
        var targetUrl = Url(target);
        var aliasUrl = Url(alias);
        var data = CFURLCreateBookmarkData(0, targetUrl, 1u << 10, 0, 0, 0);
        try {
            Assert.NotEqual(0, data);
            Assert.NotEqual(0, CFURLWriteBookmarkDataToFile(data, aliasUrl, 0, 0));
        }
        finally {
            if (data != 0) CFRelease(data);
            CFRelease(aliasUrl);
            CFRelease(targetUrl);
        }
    }
    [DllImport(Framework)] private static extern nint CFURLCreateFromFileSystemRepresentation(nint allocator, byte[] bytes, nint length, byte directory);
    [DllImport(Framework)] private static extern nint CFURLCreateBookmarkData(nint allocator, nint url, nuint options, nint properties, nint relative, nint error);
    [DllImport(Framework)] private static extern byte CFURLWriteBookmarkDataToFile(nint data, nint url, nuint options, nint error);
    [DllImport(Framework)] private static extern void CFRelease(nint value);
}
