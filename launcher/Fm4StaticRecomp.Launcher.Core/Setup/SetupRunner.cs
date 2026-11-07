// Copyright (c) 2026, i2kiselev
// BSD 3-Clause License: see LICENSE in the repository root.
using System.Diagnostics;
using System.Globalization;
using System.Text.Json;
using Fm4StaticRecomp.Launcher.Core.Install;
using Fm4StaticRecomp.Launcher.Core.Platform;

namespace Fm4StaticRecomp.Launcher.Core.Setup;

public sealed class SetupRunner(InstallLayout layout, IPlatform platform) : IBuildRunner
{
    internal Action<Process> KillTree { get; init; } = p => p.Kill(entireProcessTree: true);
    internal TimeSpan OutputDrainTimeout { get; init; } = TimeSpan.FromSeconds(10);

    public static IReadOnlyList<string> Arguments(InstallLayout layout, SetupRequest request)
    {
        if (OperatingSystem.IsMacOS())
            return [layout.SetupScript(request.Version), "--game-root", layout.Game,
                "--cache-root", layout.Cache(request.Version), "--toolchain-root", layout.Toolchain,
                "--jobs", request.BuildJobs.ToString(CultureInfo.InvariantCulture)];
        // A trailing backslash breaks the schema run's quoting inside setup.
        var arguments = new List<string>(PowerShell.FileArguments(layout.SetupScript(request.Version)))
        {
            "-GameRoot", Path.TrimEndingDirectorySeparator(layout.Game),
            "-CacheRoot", Path.TrimEndingDirectorySeparator(layout.Cache(request.Version)),
            "-JsonEvents",
            "-Parallel", request.BuildJobs.ToString(CultureInfo.InvariantCulture),
        };
        if (request.AcceptMicrosoftLicence) arguments.Add("-AcceptMicrosoftLicense");
        if (request.UseVisualStudioInstaller) arguments.AddRange(["-ToolchainSource", "VisualStudio"]);
        return arguments;
    }

    public async Task<SetupFailure?> BuildAsync(SetupRequest request, IProgress<SetupEvent> progress, Action<string> logLine,
        CancellationToken cancellation)
    {
        var start = OperatingSystem.IsMacOS() ? new ProcessStartInfo("/usr/bin/env") { UseShellExecute = false }
            : PowerShell.StartInfo(Arguments(layout, request));
        if (OperatingSystem.IsMacOS())
        {
            start.ArgumentList.Add("python3");
            foreach (var argument in Arguments(layout, request)) start.ArgumentList.Add(argument);
            start.Environment["PATH"] = "/opt/homebrew/bin:/usr/local/bin:" + Environment.GetEnvironmentVariable("PATH");
        }
        start.WorkingDirectory = layout.Source(request.Version);
        start.RedirectStandardOutput = true;
        start.RedirectStandardError = true;
        start.Environment["FM4_TOOLCHAIN_ROOT"] = layout.Toolchain;
        using var process = Process.Start(start)!;
        var gate = new object();
        void Handle(string line)
        {
            lock (gate)
            {
                if (SetupEvent.Parse(line) is { } e) progress.Report(e);
                else logLine(line);
            }
        }
        // Raw streams, not OutputDataReceived: that decodes every line with one encoding.
        var pumps = Task.WhenAll(
            ConsoleLines.PumpAsync(process.StandardOutput.BaseStream, platform.ConsoleEncoding, Handle, CancellationToken.None),
            ConsoleLines.PumpAsync(process.StandardError.BaseStream, platform.ConsoleEncoding, Handle, CancellationToken.None));
        try
        {
            await process.WaitForExitAsync(cancellation);
        }
        catch (OperationCanceledException)
        {
            // A child the launcher may not end (the elevated Visual Studio installer) is left
            // running; the rest of the tree is ended.
            try { KillTree(process); }
            catch (AggregateException) { }
            // A child left running may hold the output pipes open for as long as it runs.
            try { await pumps.WaitAsync(OutputDrainTimeout, CancellationToken.None); }
            catch (TimeoutException) { _ = pumps.ContinueWith(t => t.Exception, TaskContinuationOptions.OnlyOnFaulted); }
            throw;
        }
        await pumps;
        if (process.ExitCode == 0) return null;
        return ReadFailure(layout.SetupFailureRecord(request.Version), process.ExitCode, layout.SetupLogs(request.Version));
    }

    // The record New-Fm4FailureRecord writes (release-common.ps1).
    private static SetupFailure ReadFailure(string path, int exitCode, string logs)
    {
        var stopped = new SetupFailure("Setup stopped.", null, exitCode, [], null, null, logs);
        if (!File.Exists(path)) return stopped;
        JsonDocument document;
        try { document = JsonDocument.Parse(File.ReadAllBytes(path)); }
        catch (JsonException) { return stopped; }
        using var _ = document;
        var root = document.RootElement;
        string? Text(string name) =>
            root.TryGetProperty(name, out var v) && v.ValueKind == JsonValueKind.String ? v.GetString() : null;
        var code = root.TryGetProperty("exit_code", out var c) && c.TryGetInt32(out var n) ? n : exitCode;
        var excerpt = root.TryGetProperty("error_excerpt", out var e) && e.ValueKind == JsonValueKind.Array
            ? e.EnumerateArray().Select(x => x.GetString() ?? "").ToList()
            : [];
        return new SetupFailure(Text("message") ?? "Setup stopped.", Text("step"), code, excerpt, Text("hint"),
            Text("build_log"), logs);
    }
}
