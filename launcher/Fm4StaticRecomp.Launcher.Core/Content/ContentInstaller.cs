using System.Diagnostics;
using Fm4StaticRecomp.Launcher.Core.Discs;
using Fm4StaticRecomp.Launcher.Core.Install;
using Fm4StaticRecomp.Launcher.Core.Launch;
using Fm4StaticRecomp.Launcher.Core.Platform;
using Fm4StaticRecomp.Launcher.Core.Profiles;

namespace Fm4StaticRecomp.Launcher.Core.Content;

public sealed record InstalledPackage(string FileName, string DisplayName);

public sealed class ContentInstaller(InstallLayout layout, IPlatform platform)
{
    private string PackagesFolder => Path.Combine(layout.Content, "4D530910", "00000002");
    private string HeadersFolder => Path.Combine(layout.Content, "4D530910", "Headers", "00000002");
    private string NamesFile => Path.Combine(layout.Content, "names.toml");

    // The game installs; the launcher reads its log as it goes (a GUI executable has no
    // usable stdout) and remembers each package's display name for the Content panel.
    public async Task<ContentEvent.Finished> InstallAsync(string version, string? disc2Folder, string? dlcFolder,
        IProgress<ContentEvent> progress, CancellationToken cancellation)
    {
        Directory.CreateDirectory(layout.ContentInstallUser);
        ProfileStore.EnsureContentLink(platform, Path.Combine(layout.ContentInstallUser, ProfileStore.ContentFolderName),
            layout.Content);
        RememberNames(disc2Folder);
        RememberNames(dlcFolder);
        Directory.CreateDirectory(layout.Logs);
        var log = Path.Combine(layout.Logs, "content-install.log");
        if (File.Exists(log)) File.Delete(log);
        var start = new ProcessStartInfo(layout.GameExecutable(version)) { UseShellExecute = false,
            WorkingDirectory = layout.BuildFolder(version) };
        foreach (var argument in LaunchArguments.ForContentInstall(layout, disc2Folder, dlcFolder, log))
            start.ArgumentList.Add(argument);
        using var process = Process.Start(start)!;
        return await FollowAsync(process, log, progress, cancellation);
    }

    // Cancelling stops the game too: it would go on writing into content\ and reading the
    // staging folder the caller deletes next.
    internal static async Task<ContentEvent.Finished> FollowAsync(Process process, string log, IProgress<ContentEvent> progress,
        CancellationToken cancellation)
    {
        try
        {
            return await ReadLogAsync(process, log, progress, cancellation);
        }
        catch (OperationCanceledException)
        {
            if (!process.HasExited) process.Kill(entireProcessTree: true);
            await process.WaitForExitAsync(CancellationToken.None);
            throw;
        }
    }

    private static async Task<ContentEvent.Finished> ReadLogAsync(Process process, string log, IProgress<ContentEvent> progress,
        CancellationToken cancellation)
    {
        var linesSeen = 0;
        ContentEvent.Finished? finished = null;
        while (true)
        {
            var exited = process.HasExited;
            if (File.Exists(log))
            {
                string[] lines;
                await using (var stream = new FileStream(log, FileMode.Open, FileAccess.Read, FileShare.ReadWrite | FileShare.Delete))
                using (var reader = new StreamReader(stream))
                    lines = (await reader.ReadToEndAsync(cancellation)).Split('\n');
                foreach (var line in lines.Skip(linesSeen).Take(Math.Max(0, lines.Length - 1 - linesSeen)))
                {
                    if (ContentLog.Parse(line) is not { } e) continue;
                    progress.Report(e);
                    if (e is ContentEvent.Finished f) finished = f;
                }
                linesSeen = Math.Max(linesSeen, lines.Length - 1);
            }
            if (exited) break;
            await Task.Delay(250, cancellation);
        }
        return finished ?? throw new InvalidOperationException(
            $"The game stopped before it finished installing. See {log}.");
    }

    public IReadOnlyList<InstalledPackage> Installed()
    {
        if (!Directory.Exists(HeadersFolder)) return [];
        var names = Settings.TomlFile.Load(NamesFile);
        return Directory.GetFiles(HeadersFolder, "*.header")
            .Select(h => Path.GetFileNameWithoutExtension(h))
            .Select(f => new InstalledPackage(f, names.Get(Key(f)) ?? f))
            .OrderBy(p => p.DisplayName, StringComparer.CurrentCultureIgnoreCase).ToList();
    }

    private void RememberNames(string? folder)
    {
        if (folder is null || !Directory.Exists(folder)) return;
        var names = Settings.TomlFile.Load(NamesFile);
        foreach (var (path, header) in DiscInput.EnumeratePackages(folder))
            if (header.DisplayName.Length > 0) names.Set(Key(Path.GetFileName(path)), header.DisplayName, quoted: true);
        Directory.CreateDirectory(layout.Content);
        names.Save(NamesFile);
    }

    // TOML keys allow letters, digits and underscores only.
    private static string Key(string fileName) =>
        "p_" + new string(fileName.Select(c => char.IsAsciiLetterOrDigit(c) ? char.ToLowerInvariant(c) : '_').ToArray());
}
