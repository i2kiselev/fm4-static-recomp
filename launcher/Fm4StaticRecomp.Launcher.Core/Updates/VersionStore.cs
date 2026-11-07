// Copyright (c) 2026, i2kiselev
// BSD 3-Clause License: see LICENSE in the repository root.
using System.IO.Compression;
using System.Security.Cryptography;
using Fm4StaticRecomp.Launcher.Core.Install;
using Fm4StaticRecomp.Launcher.Core.Settings;

namespace Fm4StaticRecomp.Launcher.Core.Updates;

public sealed class VersionStore(InstallLayout layout, LauncherSettings settings)
{
    private const string PayloadMarker = ".payload-sha256";

    public string? Active => settings.ActiveVersion;
    public bool CanGoBack => settings.PreviousVersion is { } previous && Directory.Exists(layout.Source(previous));

    public async Task ExtractPayloadAsync(string zip, string version, CancellationToken cancellation)
    {
        var destination = layout.Source(version);
        string hash;
        await using (var stream = File.OpenRead(zip))
            hash = Convert.ToHexString(await SHA256.HashDataAsync(stream, cancellation));
        var marker = Path.Combine(destination, PayloadMarker);
        if (File.Exists(marker) && (await File.ReadAllTextAsync(marker, cancellation)).Trim() == hash) return;
        Directory.CreateDirectory(destination);
        await Task.Run(() =>
        {
            using var archive = ZipFile.OpenRead(zip);
            archive.ExtractToDirectory(destination, overwriteFiles: true);
            // Reproducible ZIP timestamps may predate objects from an earlier payload
            // with the same version. Make the extracted sources trigger a rebuild.
            var extractedAt = DateTime.UtcNow;
            foreach (var entry in archive.Entries)
                if (entry.Name.Length != 0)
                    File.SetLastWriteTimeUtc(Path.Combine(destination, entry.FullName), extractedAt);
        }, cancellation);
        await File.WriteAllTextAsync(marker, hash + "\n", cancellation);
    }

    public void Activate(string version)
    {
        if (settings.ActiveVersion != version)
        {
            settings.PreviousVersion = settings.ActiveVersion;
            settings.ActiveVersion = version;
        }
        settings.Save();
    }

    // Deletes the versions older than the previous one; returns the folders it could not delete
    // (a file held open). A build tree is gigabytes: run it off the UI thread.
    public IReadOnlyList<string> Prune()
    {
        var keep = new HashSet<string?>([settings.ActiveVersion, settings.PreviousVersion]);
        var left = new List<string>();
        foreach (var parent in new[] { Path.Combine(layout.Root, "source"), Path.Combine(layout.Root, "cache") })
        {
            if (!Directory.Exists(parent)) continue;
            foreach (var folder in Directory.GetDirectories(parent))
            {
                if (keep.Contains(Path.GetFileName(folder))) continue;
                try { DeleteTree(folder); }
                catch (Exception ex) when (ex is IOException or UnauthorizedAccessException) { left.Add(folder); }
            }
        }
        return left;
    }

    // Git writes its packs read-only (the SDK clone in .local), and a recursive delete stops at them.
    private static void DeleteTree(string folder)
    {
        foreach (var file in Directory.EnumerateFiles(folder, "*", new EnumerationOptions { RecurseSubdirectories = true, AttributesToSkip = 0 }))
            if (File.GetAttributes(file).HasFlag(FileAttributes.ReadOnly)) File.SetAttributes(file, FileAttributes.Normal);
        Directory.Delete(folder, recursive: true);
    }

    public void GoBack()
    {
        if (!CanGoBack) throw new InvalidOperationException("There is no previous version to go back to.");
        (settings.ActiveVersion, settings.PreviousVersion) = (settings.PreviousVersion, settings.ActiveVersion);
        settings.Save();
    }
}
