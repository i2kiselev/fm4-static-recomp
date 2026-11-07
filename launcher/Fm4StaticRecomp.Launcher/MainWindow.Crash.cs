// Portions copyright (c) 2026, Pinyon Shift contributors (BSD 3-Clause: see THIRD_PARTY_NOTICES.md).
// Copyright (c) 2026, i2kiselev (BSD 3-Clause: see LICENSE in the repository root).
// Adapted from Pinyon Shift's launcher (launcher/PinyonShift.Launcher.Desktop, BSD-3-Clause; see THIRD_PARTY_NOTICES.md).
using System.Diagnostics;
using System.Globalization;
using System.Text.Json;
using Avalonia.Interactivity;
using Fm4StaticRecomp.Launcher.Core.Setup;
using Fm4StaticRecomp.Launcher.Core.Updates;

namespace Fm4StaticRecomp.Launcher;

// The crash report offered right after an abnormal exit (spec 11): a sanitized ZIP from the
// payload's tools\create-crash-report.ps1 and a prefilled GitHub issue, unless the maintainer
// has switched the offer off in config/launcher-flags.json on the main branch.
public partial class MainWindow
{
    private sealed record CrashReport(string CrashId, string Bundle, string IssueUrl);

    private LauncherFlags _flags = LauncherFlags.Default;
    private CrashReport? _pendingReport;

    private string FlagsCache => Path.Combine(_host!.Layout.Config, "launcher-flags.json");

    private async Task RefreshFlagsAsync()
    {
        if (_host is not { } host) return;
        _flags = LauncherFlags.LoadCached(FlagsCache);
        var url = LauncherFlags.Url(host.Payload.OwnerRepo);
#if DEBUG
        // The kill switch's end-to-end test serves the flags from a local folder.
        var index = Array.IndexOf(_args, "--flags-url");
        if (index >= 0 && index + 1 < _args.Length) url = new Uri(_args[index + 1]);
#endif
        _flags = await LauncherFlags.RefreshAsync(host.Http, url, FlagsCache, CancellationToken.None);
        AppendLog($"Crash reports {(_flags.CrashReports ? "on" : "off")}.");
    }

    private async Task OfferCrashReportAsync(GameSession session, int exitCode)
    {
        var host = _host!;
        if (OperatingSystem.IsMacOS() || !_flags.CrashReports)
        {
            ShowFailure("THE GAME STOPPED", $"Exit code 0x{exitCode:X8}. The logs have the details.");
            return;
        }
        var version = session.Version;
        SetProgress(-1, "Preparing a crash report.");
        var arguments = PowerShell.FileArguments(host.Layout.CrashReportScript(version)).Concat(
        [
            "-StateRoot", host.Layout.Root,
            "-Executable", host.Layout.GameExecutable(version),
            "-StartedUtc", session.StartedUtc.ToString("o", CultureInfo.InvariantCulture),
            "-ProcessId", session.ProcessId.ToString(CultureInfo.InvariantCulture),
            "-ExitCode", exitCode.ToString(CultureInfo.InvariantCulture),
            "-ProfileId", session.Profile!.Id,
            "-Gamertag", session.Profile.Gamertag,
            "-Json",
        ]);
        var start = PowerShell.StartInfo(arguments);
        start.RedirectStandardOutput = true;
        start.RedirectStandardError = true;
        string output, error;
        int scriptExit;
        try
        {
            using var process = Process.Start(start)!;
            // Decoded as setup's output is: PowerShell writes its own errors in the OEM code page.
            List<string> outputLines = [], errorLines = [];
            var pumps = Task.WhenAll(
                ConsoleLines.PumpAsync(process.StandardOutput.BaseStream, host.Platform.ConsoleEncoding, outputLines.Add, CancellationToken.None),
                ConsoleLines.PumpAsync(process.StandardError.BaseStream, host.Platform.ConsoleEncoding, errorLines.Add, CancellationToken.None));
            await process.WaitForExitAsync();
            await pumps;
            (output, error, scriptExit) = (string.Join('\n', outputLines), string.Join('\n', errorLines), process.ExitCode);
        }
        catch (Exception failure) when (failure is System.ComponentModel.Win32Exception or IOException)
        {
            (output, error, scriptExit) = ("", failure.Message, -1);
        }
        var line = output.Split('\n').Select(l => l.Trim()).LastOrDefault(l => l.StartsWith('{'));
        if (scriptExit != 0 || line is null || ParseReport(line) is not { } report)
        {
            ShowFailure("THE GAME STOPPED",
                $"Exit code 0x{exitCode:X8}. The crash report could not be prepared: {error.Trim()}\nLogs: {host.Layout.Logs}");
            return;
        }
        ShowCrash(report);
    }

    private CrashReport? ParseReport(string line)
    {
        try
        {
            using var document = JsonDocument.Parse(line);
            var root = document.RootElement;
            var report = new CrashReport(root.GetProperty("crash_id").GetString() ?? "", root.GetProperty("bundle").GetString() ?? "",
                root.GetProperty("issue_url").GetString() ?? "");
            // Only a bundle in this install's reports folder and an issue on this project's page.
            var reports = Path.TrimEndingDirectorySeparator(Path.GetFullPath(_host!.Layout.Reports)) + Path.DirectorySeparatorChar;
            var issues = $"{Repository.TrimEnd('/')}/issues/new";
            return Path.GetFullPath(report.Bundle).StartsWith(reports, StringComparison.OrdinalIgnoreCase) && File.Exists(report.Bundle) &&
                   report.IssueUrl.StartsWith(issues, StringComparison.OrdinalIgnoreCase)
                ? report
                : null;
        }
        catch (Exception failure) when (failure is JsonException or KeyNotFoundException or InvalidOperationException) { return null; }
    }

    private void ShowCrash(CrashReport report)
    {
        _pendingReport = report;
        ShowMode(Mode.Crashed);
        Subhead.Text = "";
        CrashIdText.Text = report.CrashId;
        AppendLog($"Crash report {report.CrashId}: {report.Bundle}");
    }

    private void ReportCrash()
    {
        if (_pendingReport is not { } report) return;
        LauncherHost.Open(report.IssueUrl);
        LauncherHost.Reveal(report.Bundle);
        PrimaryButton.Content = "OPEN GITHUB AGAIN";
    }

    private void DismissCrash_Click(object? sender, RoutedEventArgs e)
    {
        _pendingReport = null;
        ShowMode(Mode.Ready);
    }
}
