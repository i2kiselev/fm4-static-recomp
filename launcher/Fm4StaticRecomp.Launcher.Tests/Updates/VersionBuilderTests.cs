// Copyright (c) 2026, i2kiselev
// BSD 3-Clause License: see LICENSE in the repository root.
using System.IO.Compression;
using Fm4StaticRecomp.Launcher.Core.Install;
using Fm4StaticRecomp.Launcher.Core.Settings;
using Fm4StaticRecomp.Launcher.Core.Setup;
using Fm4StaticRecomp.Launcher.Core.Updates;

namespace Fm4StaticRecomp.Launcher.Tests.Updates;

public class VersionBuilderTests
{
    private sealed class FakeRunner(Func<SetupRequest, int, SetupFailure?> result) : IBuildRunner
    {
        public int Calls { get; private set; }

        public Task<SetupFailure?> BuildAsync(SetupRequest request, IProgress<SetupEvent> progress, Action<string> logLine,
            CancellationToken cancellation) => Task.FromResult(result(request, ++Calls));
    }

    private static SetupFailure Fail => new("boom", "build", 1, [], null, null, "");

    private static SetupRequest Request(string version) => new(version, 0, true, false);

    private static string Zip(TempDir temp, string name, bool withSetup = true)
    {
        var folder = Path.Combine(temp.Path, name);
        Directory.CreateDirectory(Path.Combine(folder, "tools"));
        if (withSetup) File.WriteAllText(Path.Combine(folder, "tools", OperatingSystem.IsMacOS() ? "setup-macos.py" : "setup.ps1"), name);
        else File.WriteAllText(Path.Combine(folder, "tools", "other.ps1"), name);
        ZipFile.CreateFromDirectory(folder, folder + ".zip");
        return folder + ".zip";
    }

    private static (InstallLayout, LauncherSettings, SetupState, VersionStore) Make(TempDir temp)
    {
        var layout = new InstallLayout(Path.Combine(temp.Path, "install"));
        Directory.CreateDirectory(layout.Root);
        var settings = LauncherSettings.Load(layout.LauncherToml);
        return (layout, settings, SetupState.Load(layout.SetupState), new VersionStore(layout, settings));
    }

    [Fact]
    public async Task A_successful_build_is_recorded_and_activated()
    {
        using var temp = new TempDir();
        var (layout, settings, state, versions) = Make(temp);
        var runner = new FakeRunner((_, _) => null);
        Assert.Null(await new VersionBuilder(layout, versions, state, runner).BuildVersionAsync(Request("0.1.0"), Zip(temp, "a"),
            new SyncProgress<SetupEvent>(_ => { }), _ => { }, CancellationToken.None));
        Assert.Contains("0.1.0", SetupState.Load(layout.SetupState).BuiltVersions);
        Assert.Equal("0.1.0", settings.ActiveVersion);
        Assert.Equal(1, runner.Calls);
    }

    [Fact]
    public async Task A_failed_update_keeps_the_old_version_active_and_untouched()
    {
        using var temp = new TempDir();
        var (layout, settings, state, versions) = Make(temp);
        await new VersionBuilder(layout, versions, state, new FakeRunner((_, _) => null)).BuildVersionAsync(Request("0.1.0"),
            Zip(temp, "a"), new SyncProgress<SetupEvent>(_ => { }), _ => { }, CancellationToken.None);
        var oldExe = temp.File(Path.Combine("install", "source", "0.1.0", "out", "build", "win-amd64-release", "fm4_static_recomp.exe"), [1]);
        var runner = new FakeRunner((_, _) => Fail);
        var failure = await new VersionBuilder(layout, versions, state, runner).BuildVersionAsync(Request("0.2.0"), Zip(temp, "b"),
            new SyncProgress<SetupEvent>(_ => { }), _ => { }, CancellationToken.None);
        Assert.NotNull(failure);
        Assert.Equal(1, runner.Calls);
        Assert.Equal("0.1.0", settings.ActiveVersion);
        Assert.Equal([1], File.ReadAllBytes(oldExe));
        Assert.DoesNotContain("0.2.0", SetupState.Load(layout.SetupState).BuiltVersions);
    }

    [Fact]
    public async Task A_payload_without_setup_is_a_failure_and_runs_nothing()
    {
        using var temp = new TempDir();
        var (layout, settings, state, versions) = Make(temp);
        var runner = new FakeRunner((_, _) => null);
        var failure = await new VersionBuilder(layout, versions, state, runner).BuildVersionAsync(Request("0.1.0"),
            Zip(temp, "a", withSetup: false), new SyncProgress<SetupEvent>(_ => { }), _ => { }, CancellationToken.None);
        Assert.Contains(OperatingSystem.IsMacOS() ? "setup-macos.py" : "setup.ps1", failure!.Message);
        Assert.Equal(0, runner.Calls);
        Assert.Null(settings.ActiveVersion);
    }

    [Fact]
    public async Task An_old_version_that_cannot_be_deleted_does_not_undo_the_switch()
    {
        using var temp = new TempDir();
        var (layout, settings, state, versions) = Make(temp);
        var builder = new VersionBuilder(layout, versions, state, new FakeRunner((_, _) => null));
        var lines = new List<string>();
        foreach (var (version, zip) in new[] { ("0.1.0", "a"), ("0.2.0", "b") })
            await builder.BuildVersionAsync(Request(version), Zip(temp, zip), new SyncProgress<SetupEvent>(_ => { }), lines.Add,
                CancellationToken.None);
        // A virus scanner or Explorer holding a file of the version that goes.
        var held = temp.File(Path.Combine("install", "source", "0.1.0", "out", "held.o"), [1]);
        SetupFailure? failure;
        using (File.Open(held, FileMode.Open, FileAccess.Read, FileShare.None))
            failure = await builder.BuildVersionAsync(Request("0.3.0"), Zip(temp, "c"), new SyncProgress<SetupEvent>(_ => { }),
                lines.Add, CancellationToken.None);
        Assert.Null(failure);
        Assert.Equal("0.3.0", settings.ActiveVersion);
        if (OperatingSystem.IsWindows()) Assert.Contains(lines, l => l.Contains("0.1.0") && l.Contains("could not be deleted"));
    }
}
