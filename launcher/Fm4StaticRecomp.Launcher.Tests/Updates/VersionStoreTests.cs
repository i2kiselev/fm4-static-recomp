// Copyright (c) 2026, i2kiselev
// BSD 3-Clause License: see LICENSE in the repository root.
using System.IO.Compression;
using Fm4StaticRecomp.Launcher.Core.Install;
using Fm4StaticRecomp.Launcher.Core.Settings;
using Fm4StaticRecomp.Launcher.Core.Updates;

namespace Fm4StaticRecomp.Launcher.Tests.Updates;

public class VersionStoreTests
{
    private static string Zip(TempDir temp, string name, string marker)
    {
        var folder = Path.Combine(temp.Path, name + "-content");
        Directory.CreateDirectory(Path.Combine(folder, "tools"));
        File.WriteAllText(Path.Combine(folder, "tools", "setup.ps1"), marker);
        var zip = Path.Combine(temp.Path, name + ".zip");
        ZipFile.CreateFromDirectory(folder, zip);
        return zip;
    }

    [Fact]
    public async Task Activating_keeps_one_previous_version_and_go_back_swaps()
    {
        using var temp = new TempDir();
        var layout = new InstallLayout(Path.Combine(temp.Path, "install"));
        var settings = LauncherSettings.Load(layout.LauncherToml);
        var store = new VersionStore(layout, settings);
        foreach (var v in new[] { "0.1.0", "0.2.0", "0.3.0" })
        {
            await store.ExtractPayloadAsync(Zip(temp, v, v), v, CancellationToken.None);
            Directory.CreateDirectory(layout.Cache(v));
            store.Activate(v);
            Assert.Empty(store.Prune());
        }
        Assert.Equal("0.3.0", store.Active);
        Assert.False(Directory.Exists(layout.Source("0.1.0")));
        Assert.False(Directory.Exists(layout.Cache("0.1.0")));
        Assert.True(Directory.Exists(layout.Source("0.2.0")));
        store.GoBack();
        Assert.Equal("0.2.0", LauncherSettings.Load(layout.LauncherToml).ActiveVersion);
        Assert.Equal("0.3.0", LauncherSettings.Load(layout.LauncherToml).PreviousVersion);
    }

    [Fact]
    public void Prunes_a_version_whose_sdk_clone_holds_read_only_git_packs()
    {
        using var temp = new TempDir();
        var layout = new InstallLayout(Path.Combine(temp.Path, "install"));
        var settings = LauncherSettings.Load(layout.LauncherToml);
        var store = new VersionStore(layout, settings);
        var pack = Path.Combine(layout.Source("0.1.0"), ".local", "shiftglue-sdk", ".git", "objects", "pack", "pack-1.pack");
        Directory.CreateDirectory(Path.GetDirectoryName(pack)!);
        File.WriteAllBytes(pack, [1]);
        File.SetAttributes(pack, FileAttributes.ReadOnly);
        Directory.CreateDirectory(layout.Source("0.2.0"));
        Directory.CreateDirectory(layout.Source("0.3.0"));
        (settings.ActiveVersion, settings.PreviousVersion) = ("0.3.0", "0.2.0");
        Assert.Empty(store.Prune());
        Assert.False(Directory.Exists(layout.Source("0.1.0")));
    }

    [Fact]
    public async Task Changed_payload_with_old_zip_timestamps_invalidates_existing_objects()
    {
        using var temp = new TempDir();
        var layout = new InstallLayout(Path.Combine(temp.Path, "install"));
        var store = new VersionStore(layout, LauncherSettings.Load(layout.LauncherToml));
        await store.ExtractPayloadAsync(Zip(temp, "first", "old source"), "0.1.0", CancellationToken.None);
        var built = temp.File(Path.Combine("install", "source", "0.1.0", "out", "keep.o"), [1]);
        var builtAt = DateTime.UtcNow.AddDays(-1);
        File.SetLastWriteTimeUtc(built, builtAt);
        var replacement = Zip(temp, "replacement", "new source");
        using (var archive = ZipFile.Open(replacement, ZipArchiveMode.Update))
            foreach (var entry in archive.Entries)
                entry.LastWriteTime = new DateTimeOffset(2020, 1, 1, 0, 0, 0, TimeSpan.Zero);

        await store.ExtractPayloadAsync(replacement, "0.1.0", CancellationToken.None);

        var source = Path.Combine(layout.Source("0.1.0"), "tools", "setup.ps1");
        Assert.Equal("new source", File.ReadAllText(source));
        Assert.True(File.GetLastWriteTimeUtc(source) > File.GetLastWriteTimeUtc(built));
        Assert.Equal(builtAt, File.GetLastWriteTimeUtc(built));
    }

    [Fact]
    public async Task Re_extracts_only_a_changed_payload()
    {
        using var temp = new TempDir();
        var layout = new InstallLayout(Path.Combine(temp.Path, "install"));
        var store = new VersionStore(layout, LauncherSettings.Load(layout.LauncherToml));
        var zip = Zip(temp, "a", "first");
        await store.ExtractPayloadAsync(zip, "0.1.0", CancellationToken.None);
        var built = temp.File(Path.Combine("install", "source", "0.1.0", "out", "keep.o"), [1]);
        await store.ExtractPayloadAsync(zip, "0.1.0", CancellationToken.None);
        Assert.True(File.Exists(built));
    }
}
