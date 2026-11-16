// Copyright (c) 2026, i2kiselev
// BSD 3-Clause License: see LICENSE in the repository root.
using System.Security.Cryptography;

namespace Fm4StaticRecomp.Launcher.Core.Discs;

public enum FileProblemKind { Missing, SizeMismatch, HashMismatch }

public sealed record FileProblem(string Path, FileProblemKind Kind);

public sealed record RequiredCheck(Disc1Release? Release, string MediaId, string FoundSha256, string? ExpectedSha256)
{
    public bool Matches => Release is not null && ExpectedSha256 is not null &&
                           FoundSha256.Equals(ExpectedSha256, StringComparison.OrdinalIgnoreCase);
}

public static class Disc1Installer
{
    public const string Executable = "default.xex";

    // The release is chosen by the GOD header's media ID when there is one, else by the
    // executable's own; its default.xex must then match exactly.
    public static async Task<RequiredCheck> CheckRequiredAsync(IDiscFiles files, DiscManifest manifest, string? headerMediaId,
        CancellationToken cancellation)
    {
        var executable = files.Find(Executable);
        var found = "";
        string? xexMediaId = null;
        if (executable is not null)
        {
            await using var stream = files.Open(executable);
            xexMediaId = XexInfo.MediaId(stream);
            stream.Position = 0;
            found = Convert.ToHexString(await SHA256.HashDataAsync(stream, cancellation));
        }
        var mediaId = headerMediaId ?? xexMediaId ?? "";
        var release = manifest.Release(mediaId);
        var expected = release?.Files.FirstOrDefault(f => string.Equals(f.Path, Executable, StringComparison.OrdinalIgnoreCase));
        return new RequiredCheck(release, mediaId, found, expected?.Sha256);
    }

    // Writes every file of the disc under `destination` and compares each with the manifest
    // while copying, so a 7.6 GB disc is read once.
    public static async Task<IReadOnlyList<FileProblem>> ExtractAsync(IDiscFiles files, DiscManifest manifest,
        Disc1Release release, string destination, IProgress<double>? progress, CancellationToken cancellation)
    {
        var expected = manifest.Disc1Files(release).ToDictionary(f => f.Path, StringComparer.OrdinalIgnoreCase);
        var problems = new List<FileProblem>();
        var seen = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
        var spelling = ManifestSpelling(expected.Keys);
        long total = Math.Max(1, files.Files.Sum(f => f.Size)), done = 0;
        var buffer = new byte[1 << 20];
        foreach (var file in files.Files)
        {
            cancellation.ThrowIfCancellationRequested();
            var target = SafePath.Combine(destination, Respell(file.Path, spelling));
            Directory.CreateDirectory(Path.GetDirectoryName(target)!);
            using var hash = IncrementalHash.CreateHash(HashAlgorithmName.SHA256);
            await using (var input = files.Open(file))
            await using (var output = File.Create(target))
            {
                int read;
                while ((read = await input.ReadAsync(buffer, cancellation)) > 0)
                {
                    hash.AppendData(buffer, 0, read);
                    await output.WriteAsync(buffer.AsMemory(0, read), cancellation);
                    done += read;
                    progress?.Report((double)done / total);
                }
            }
            seen.Add(file.Path);
            if (!expected.TryGetValue(file.Path, out var known)) continue;
            if (known.Size != file.Size) problems.Add(new FileProblem(known.Path, FileProblemKind.SizeMismatch));
            else if (!Convert.ToHexString(hash.GetHashAndReset()).Equals(known.Sha256, StringComparison.OrdinalIgnoreCase))
                problems.Add(new FileProblem(known.Path, FileProblemKind.HashMismatch));
        }
        problems.AddRange(expected.Values.Where(f => !f.Optional && !seen.Contains(f.Path))
            .Select(f => new FileProblem(f.Path, FileProblemKind.Missing)));
        return problems.OrderBy(p => p.Path, StringComparer.Ordinal).ToList();
    }

    // Discs differ in folder case (rev 1 Europe has MEDIA, the manifest Media). Windows and Mac
    // ignore it; Linux would then have MEDIA while the game's host code opens Media/UI and
    // Media/Shaders. So write every folder and file with the manifest's spelling when it has one.
    static Dictionary<string, string> ManifestSpelling(IEnumerable<string> paths)
    {
        var spelling = new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase);
        foreach (var path in paths)
            for (var end = path.IndexOf('/'); ; end = path.IndexOf('/', end + 1))
            {
                var prefix = end < 0 ? path : path[..end];
                spelling.TryAdd(prefix, prefix);
                if (end < 0) break;
            }
        return spelling;
    }

    static string Respell(string path, Dictionary<string, string> spelling)
    {
        var parts = path.Split('/');
        for (var i = parts.Length; i > 0; i--)
            if (spelling.TryGetValue(string.Join('/', parts[..i]), out var known))
                return i == parts.Length ? known : known + "/" + string.Join('/', parts[i..]);
        return path;
    }
}
