// Copyright (c) 2026, i2kiselev
// BSD 3-Clause License: see LICENSE in the repository root.
using System.Diagnostics;
using System.Text;
using Fm4StaticRecomp.Launcher.Core.Install;
using Fm4StaticRecomp.Launcher.Core.Platform;
using Fm4StaticRecomp.Launcher.Core.Setup;

namespace Fm4StaticRecomp.Launcher.Tests.Setup;

public class SetupRunnerTests
{
    private const string Params = """
        param([string]$GameRoot, [string]$CacheRoot, [switch]$JsonEvents, [int]$Parallel,
              [switch]$AcceptMicrosoftLicense, [string]$ToolchainSource)

        """;

    // Windows PowerShell reads a script without a byte order mark in the ANSI code page.
    private static byte[] Script(string text) => [.. Encoding.UTF8.GetPreamble(), .. Encoding.UTF8.GetBytes(Params + text)];

    [Fact]
    public void Parses_event_lines_and_ignores_others()
    {
        Assert.Equal(new SetupEvent("build", 70, "Compiling FM4 Static Recomp."),
            SetupEvent.Parse("::fm4::{\"stage\":\"build\",\"percent\":70,\"message\":\"Compiling FM4 Static Recomp.\"}"));
        Assert.Null(SetupEvent.Parse("[BUILD] Compiling"));
        Assert.Null(SetupEvent.Parse("::fm4::{not json"));
    }

    [Theory]
    [InlineData("tools", 32, 28.8)]
    [InlineData("schema", 100, 90)]
    [InlineData("fonts", 100, 90)]
    [InlineData("shaders", 50, 95)]
    [InlineData("other", 10, -1)]
    public void Weighs_the_stages(string stage, int percent, double overall) =>
        Assert.Equal(overall, SetupProgress.Overall(new SetupEvent(stage, percent, "")), 3);

    [Fact]
    public void Passes_the_layout_and_the_player_s_choices()
    {
        if (!OperatingSystem.IsWindows()) return;
        var layout = new InstallLayout(@"C:\G\data");
        var args = SetupRunner.Arguments(layout, new SetupRequest("0.2.0", 6, true, false));
        Assert.Equal(["-GameRoot", @"C:\G\data\game", "-CacheRoot", @"C:\G\data\cache\0.2.0", "-JsonEvents",
            "-Parallel", "6", "-AcceptMicrosoftLicense"], args.SkipWhile(a => a != "-GameRoot"));
        Assert.Equal(["-NoLogo", "-NoProfile", "-ExecutionPolicy", "Bypass", "-File", @"C:\G\data\source\0.2.0\tools\setup.ps1"],
            args.TakeWhile(a => a != "-GameRoot"));
        Assert.DoesNotContain(args, a => a.EndsWith('\\'));
        Assert.Equal(["-ToolchainSource", "VisualStudio"], SetupRunner.Arguments(layout,
            new SetupRequest("0.2.0", 0, false, true)).SkipWhile(a => a != "-ToolchainSource"));
    }

    [Fact]
    public async Task Runs_the_version_s_setup_script_and_reports_failure()
    {
        if (!OperatingSystem.IsWindows()) return;
        using var temp = new TempDir();
        var layout = new InstallLayout(temp.Path);
        temp.File("source/1.0.0/tools/setup.ps1", Script("""
            Write-Output ('::fm4::{"stage":"tools","percent":5,"message":"' + $env:FM4_TOOLCHAIN_ROOT.Replace('\','/') + '"}')
            [Console]::Out.WriteLine('Путь: ' + $CacheRoot)
            New-Item -ItemType Directory -Force (Join-Path $PSScriptRoot '../.local/logs') | Out-Null
            '{"message":"The licence was not accepted.","step":"licence","exit_code":2,"error_excerpt":["error: x"],"hint":"Accept it."}' |
                Set-Content (Join-Path $PSScriptRoot '../.local/logs/setup-error.json')
            exit 1
            """));
        var events = new List<SetupEvent>();
        var lines = new List<string>();
        var failure = await new SetupRunner(layout, Platforms.Current).BuildAsync(new SetupRequest("1.0.0", 0, false, false),
            new SyncProgress<SetupEvent>(events.Add), lines.Add, CancellationToken.None);
        Assert.Equal(layout.Toolchain.Replace('\\', '/'), events.Single().Message);
        Assert.Contains("Путь: " + layout.Cache("1.0.0"), lines);
        Assert.NotNull(failure);
        Assert.Equal("The licence was not accepted.", failure!.Message);
        Assert.Equal("licence", failure.Step);
        Assert.Equal(2, failure.ExitCode);
        Assert.Equal(["error: x"], failure.Excerpt);
        Assert.Equal("Accept it.", failure.Hint);
        Assert.Equal(layout.SetupLogs("1.0.0"), failure.LogFolder);
        Assert.True(failure.OffersVisualStudioInstaller);
    }

    [Fact]
    public async Task A_cut_off_failure_record_reads_as_a_plain_stop()
    {
        if (!OperatingSystem.IsWindows()) return;
        using var temp = new TempDir();
        var layout = new InstallLayout(temp.Path);
        temp.File("source/1.0.0/tools/setup.ps1", Script("""
            New-Item -ItemType Directory -Force (Join-Path $PSScriptRoot '../.local/logs') | Out-Null
            '{"message":"The lic' | Set-Content (Join-Path $PSScriptRoot '../.local/logs/setup-error.json')
            exit 3
            """));
        var failure = await new SetupRunner(layout, Platforms.Current).BuildAsync(new SetupRequest("1.0.0", 0, false, false),
            new SyncProgress<SetupEvent>(_ => { }), _ => { }, CancellationToken.None);
        Assert.Equal("Setup stopped.", failure!.Message);
        Assert.Equal(3, failure.ExitCode);
    }

    [Fact]
    public async Task A_successful_run_returns_no_failure()
    {
        if (!OperatingSystem.IsWindows()) return;
        using var temp = new TempDir();
        var layout = new InstallLayout(temp.Path);
        temp.File("source/1.0.0/tools/setup.ps1", Script("exit 0"));
        Assert.Null(await new SetupRunner(layout, Platforms.Current).BuildAsync(new SetupRequest("1.0.0", 0, false, false),
            new SyncProgress<SetupEvent>(_ => { }), _ => { }, CancellationToken.None));
    }

    [Fact]
    public async Task Cancelling_kills_the_script()
    {
        if (!OperatingSystem.IsWindows()) return;
        using var temp = new TempDir();
        var layout = new InstallLayout(temp.Path);
        var pidFile = Path.Combine(temp.Path, "pid.txt");
        temp.File("source/1.0.0/tools/setup.ps1", Script($$"""
            Set-Content -LiteralPath '{{pidFile}}' -Value $PID
            Write-Output '::fm4::{"stage":"tools","percent":1,"message":"start"}'
            Start-Sleep -Seconds 60
            """));
        using var cancel = new CancellationTokenSource();
        var clock = Stopwatch.StartNew();
        await Assert.ThrowsAnyAsync<OperationCanceledException>(() => new SetupRunner(layout, Platforms.Current).BuildAsync(
            new SetupRequest("1.0.0", 0, false, false), new SyncProgress<SetupEvent>(_ => cancel.Cancel()), _ => { },
            cancel.Token));
        Assert.True(clock.Elapsed < TimeSpan.FromSeconds(10), clock.Elapsed.ToString());
        var pid = int.Parse(File.ReadAllText(pidFile).Trim());
        Assert.Throws<ArgumentException>(() => Process.GetProcessById(pid));
    }

    [Fact]
    public async Task Cancelling_stays_a_cancel_when_a_child_cannot_be_ended()
    {
        if (!OperatingSystem.IsWindows()) return;
        using var temp = new TempDir();
        var layout = new InstallLayout(temp.Path);
        temp.File("source/1.0.0/tools/setup.ps1", Script("""
            Write-Output '::fm4::{"stage":"tools","percent":1,"message":"start"}'
            Start-Sleep -Seconds 60
            """));
        var runner = new SetupRunner(layout, Platforms.Current)
        {
            KillTree = p =>
            {
                p.Kill(entireProcessTree: true);
                throw new AggregateException(new System.ComponentModel.Win32Exception(5));
            },
        };
        using var cancel = new CancellationTokenSource();
        await Assert.ThrowsAnyAsync<OperationCanceledException>(() => runner.BuildAsync(new SetupRequest("1.0.0", 0, false, false),
            new SyncProgress<SetupEvent>(_ => cancel.Cancel()), _ => { }, cancel.Token));
    }

    [Fact]
    public async Task Cancelling_does_not_wait_forever_for_a_process_that_keeps_the_output_open()
    {
        if (!OperatingSystem.IsWindows()) return;
        using var temp = new TempDir();
        var layout = new InstallLayout(temp.Path);
        var pidFile = Path.Combine(temp.Path, "pid.txt");
        temp.File("source/1.0.0/tools/setup.ps1", Script($$"""
            Set-Content -LiteralPath '{{pidFile}}' -Value $PID
            Write-Output '::fm4::{"stage":"tools","percent":1,"message":"start"}'
            Start-Sleep -Seconds 60
            """));
        // Stands in for a child the kill cannot reach that holds setup's output pipes.
        var runner = new SetupRunner(layout, Platforms.Current) { KillTree = _ => { }, OutputDrainTimeout = TimeSpan.FromSeconds(1) };
        using var cancel = new CancellationTokenSource();
        var clock = Stopwatch.StartNew();
        try
        {
            await Assert.ThrowsAnyAsync<OperationCanceledException>(() => runner.BuildAsync(new SetupRequest("1.0.0", 0, false, false),
                new SyncProgress<SetupEvent>(_ => cancel.Cancel()), _ => { }, cancel.Token));
            Assert.True(clock.Elapsed < TimeSpan.FromSeconds(15), clock.Elapsed.ToString());
        }
        finally
        {
            try { Process.GetProcessById(int.Parse(File.ReadAllText(pidFile).Trim())).Kill(); }
            catch (ArgumentException) { }
        }
    }
}
