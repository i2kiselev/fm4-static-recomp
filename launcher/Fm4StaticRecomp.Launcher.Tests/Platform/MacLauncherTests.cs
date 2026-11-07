// Copyright (c) 2026, i2kiselev
// BSD 3-Clause License: see LICENSE in the repository root.
using Fm4StaticRecomp.Launcher.Core.Install;
using Fm4StaticRecomp.Launcher.Core.Platform;
using Fm4StaticRecomp.Launcher.Core.Setup;

namespace Fm4StaticRecomp.Launcher.Tests.Platform;

public class MacLauncherTests
{
    [Fact]
    public void Bundle_keeps_data_beside_app_even_with_spaces_in_path()
    {
        if (OperatingSystem.IsWindows()) return;
        var layout = InstallLayout.ForLauncher("/Users/player/My Games/FM4.app/Contents/MacOS");
        Assert.Equal("/Users/player/My Games/data", layout.Root);
    }

    [Fact]
    public void Unbundled_launcher_keeps_data_beside_executable()
    {
        using var temp = new TempDir();
        Assert.Equal(Path.Combine(temp.Path, "data"), InstallLayout.ForLauncher(temp.Path).Root);
    }

    [Fact]
    public void Mac_setup_uses_native_paths_and_separate_arguments()
    {
        if (!OperatingSystem.IsMacOS()) return;
        using var temp = new TempDir();
        var layout = new InstallLayout(Path.Combine(temp.Path, "My Games"));
        Assert.EndsWith("out/build/mac-arm64-release/fm4_static_recomp", layout.GameExecutable("0.3.0"));
        var arguments = SetupRunner.Arguments(layout, new SetupRequest("0.3.0", 4, false, false));
        Assert.Equal(layout.SetupScript("0.3.0"), arguments[0]);
        Assert.Contains(layout.Game, arguments);
        Assert.Contains("--jobs", arguments);
        Assert.DoesNotContain("-AcceptMicrosoftLicense", arguments);
    }

    [Fact]
    public void Unix_root_accepts_long_paths_and_probes_symlinks()
    {
        if (OperatingSystem.IsWindows()) return;
        using var temp = new TempDir();
        var root = Path.Combine(temp.Path, new string('a', 80), "data");
        Assert.Null(RootCheck.Problem(root, new RootEnvironment([], "/unrelated-temp",
            new LinuxPlatform().CreateDirectoryLink, WindowsPathLimits: false)));
    }
    [Fact]
    public async Task Native_setup_reports_progress_and_a_structured_failure_from_python()
    {
        if (!OperatingSystem.IsMacOS()) return;
        using var temp = new TempDir();
        var layout = new InstallLayout(Path.Combine(temp.Path, "My Games"));
        var script = layout.SetupScript("0.3.0");
        Directory.CreateDirectory(Path.GetDirectoryName(script)!);
        await File.WriteAllTextAsync(script, """
            import json, pathlib, sys
            assert sys.argv[1:3] == ['--game-root', sys.argv[2]]
            assert 'My Games' in sys.argv[2]
            print('::fm4::' + json.dumps(dict(stage='sdk', percent=15, message='Native setup')))
            print('compiler diagnostic', file=sys.stderr)
            logs = pathlib.Path('.local/logs')
            logs.mkdir(parents=True)
            (logs / 'setup-error.json').write_text(json.dumps(dict(message='SDK build stopped', step='sdk', exit_code=7)))
            sys.exit(7)
            """);
        var events = new List<SetupEvent>();
        var log = new List<string>();
        var failure = await new SetupRunner(layout, new LinuxPlatform()).BuildAsync(
            new SetupRequest("0.3.0", 4, false, false), new SyncProgress<SetupEvent>(events.Add),
            log.Add, CancellationToken.None);
        Assert.Equal("SDK build stopped", failure!.Message);
        Assert.Equal(7, failure.ExitCode);
        Assert.Equal("Native setup", Assert.Single(events).Message);
        Assert.Contains("compiler diagnostic", log);
    }
}
