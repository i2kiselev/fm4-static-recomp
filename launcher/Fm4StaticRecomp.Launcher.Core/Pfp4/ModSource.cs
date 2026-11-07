// Copyright (c) 2026, i2kiselev
// BSD 3-Clause License: see LICENSE in the repository root.
using System.IO.Compression;

namespace Fm4StaticRecomp.Launcher.Core.Pfp4;

// Project Forza Plus 4 as the player has it: the zip as downloaded (~9 GB, read entry by entry,
// never extracted) or an extracted copy. Paths are relative to the mod's root folder, the one
// holding `Main Game Changes\default.xex`, and `\`-separated.
public sealed class ModSource : IDisposable
{
    public const string RootName = "Project Forza Plus 4";
    private const string Marker = @"Main Game Changes\default.xex";
    // The root itself, its parent, or a wrapper folder above that (a zip packed from one level up).
    private const int SearchDepth = 2;

    private readonly ZipArchive? _zip;
    private readonly Dictionary<string, ZipArchiveEntry> _entries = new(StringComparer.OrdinalIgnoreCase);
    private readonly List<string> _files = [];
    private readonly Dictionary<string, string> _diskFiles = new(StringComparer.OrdinalIgnoreCase);

    private ModSource(ZipArchive zip, string prefix)
    {
        _zip = zip;
        foreach (var entry in zip.Entries)
        {
            var name = entry.FullName.Replace('/', '\\');
            if (name.EndsWith('\\') || !name.StartsWith(prefix, StringComparison.OrdinalIgnoreCase)) continue;
            var relative = name[prefix.Length..];
            _entries[relative] = entry;
            _files.Add(relative);
        }
    }

    private ModSource(string folder)
    {
        var options = new EnumerationOptions { RecurseSubdirectories = true, AttributesToSkip = 0 };
        foreach (var file in Directory.EnumerateFiles(folder, "*", options))
        {
            var relative = Path.GetRelativePath(folder, file).Replace(Path.DirectorySeparatorChar, '\\');
            _files.Add(relative);
            _diskFiles.Add(relative, file);
        }
    }

    public IReadOnlyList<string> Files => _files;

    public static ModSource Open(string zipOrFolder)
    {
        if (File.Exists(zipOrFolder))
        {
            var zip = ZipFile.OpenRead(zipOrFolder);
            var markers = zip.Entries.Select(e => e.FullName.Replace('/', '\\'))
                .Where(n => n.EndsWith(Marker, StringComparison.OrdinalIgnoreCase))
                .Select(n => n[..^Marker.Length])
                .Where(prefix => prefix.Count(c => c == '\\') <= SearchDepth)
                .OrderBy(prefix => prefix.Count(c => c == '\\')).ThenBy(prefix => prefix, StringComparer.Ordinal)
                .ToList();
            if (markers.Count == 0)
            {
                zip.Dispose();
                throw NotTheMod(zipOrFolder);
            }
            return new ModSource(zip, markers[0]);
        }
        var level = new List<string> { zipOrFolder };
        for (var depth = 0; depth <= SearchDepth && level.Count > 0; depth++)
        {
            var root = level.Order(StringComparer.Ordinal).FirstOrDefault(d => File.Exists(Path.Combine(d, Marker.Replace('\\', Path.DirectorySeparatorChar))));
            if (root is not null) return new ModSource(root);
            level = level.Where(Directory.Exists).SelectMany(Directory.EnumerateDirectories).ToList();
        }
        throw NotTheMod(zipOrFolder);
    }

    private static InvalidDataException NotTheMod(string path) =>
        new($"{path} does not hold Project Forza Plus 4: expected its zip, or the folder \"{RootName}\" " +
            $"(the one with \"{Marker}\") or the folder above it.");

    public Stream OpenFile(string relativePath) => _zip is null
        ? File.OpenRead(OnDisk(relativePath))
        : Entry(relativePath).Open();

    public long Size(string relativePath) => _zip is null
        ? new FileInfo(OnDisk(relativePath)).Length
        : Entry(relativePath).Length;

    private string OnDisk(string relativePath) => _diskFiles.TryGetValue(relativePath, out var path)
        ? path : throw new FileNotFoundException($"{relativePath} is not in the mod's folder.", relativePath);

    private ZipArchiveEntry Entry(string relativePath) =>
        _entries.TryGetValue(relativePath, out var entry)
            ? entry
            : throw new FileNotFoundException($"{relativePath} is not in the mod's zip.", relativePath);

    public void Dispose() => _zip?.Dispose();
}
