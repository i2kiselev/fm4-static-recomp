using System.Diagnostics;
using Fm4StaticRecomp.Launcher.Core.Launch;
using Fm4StaticRecomp.Launcher.Core.Profiles;
using Fm4StaticRecomp.Launcher.Core.Updates;

namespace Fm4StaticRecomp.Launcher;

// One run of the active version on one profile; GameRunning is set for its whole length.
public sealed class GameSession(LauncherHost host)
{
    public DateTime StartedUtc { get; private set; }
    public int ProcessId { get; private set; }
    // An update's build can switch the active version while the game runs.
    public string Version { get; private set; } = "";
    public Profile? Profile { get; private set; }

    public async Task<int> RunAsync(Profile profile)
    {
        var version = host.Settings.ActiveVersion ?? throw new InvalidOperationException("No version is built.");
        (Version, Profile) = (version, profile);
        host.GameRunning = true;
        try
        {
            await Task.Run(() =>
            {
                host.Profiles.EnsureContentLink(profile);
                PlayPreparation.BackupIfNeeded(profile, version, host.Profiles, host.Layout, host.Platform);
            });
            Directory.CreateDirectory(host.Layout.Logs);
            var start = new ProcessStartInfo(host.Layout.GameExecutable(version))
            {
                UseShellExecute = false,
                WorkingDirectory = host.Layout.BuildFolder(version),
            };
            foreach (var argument in LaunchArguments.ForPlay(host.Layout, version, profile, host.Settings, host.Platform))
                start.ArgumentList.Add(argument);
            StartedUtc = DateTime.UtcNow;
            using var process = Process.Start(start)!;
            ProcessId = process.Id;
            await process.WaitForExitAsync();
            return process.ExitCode;
        }
        finally
        {
            host.GameRunning = false;
        }
    }
}
