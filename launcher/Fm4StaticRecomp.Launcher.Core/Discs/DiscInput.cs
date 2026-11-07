// Copyright (c) 2026, i2kiselev
// BSD 3-Clause License: see LICENSE in the repository root.
using Fm4StaticRecomp.Launcher.Core.Platform;

namespace Fm4StaticRecomp.Launcher.Core.Discs;

public enum DiscInputKind { Iso, God, ExtractedFolder, PackageFolder }

public sealed record DiscInput(DiscInputKind Kind, string Path, string? HeaderMediaId)
{
    // A drive root or Documents holds folders the player cannot open (System Volume Information).
    internal static readonly EnumerationOptions Everything = new()
    {
        RecurseSubdirectories = true,
        IgnoreInaccessible = true,
        AttributesToSkip = 0,
    };

    // Whatever the player picks: a file or folder that cannot be read is no disc, not an error.
    public static DiscInput? Detect(string path)
    {
        try { return DetectReadable(SourcePath.Resolve(path)); }
        catch (Exception ex) when (ex is IOException or UnauthorizedAccessException or InvalidDataException or OverflowException)
        {
            return null;
        }
    }

    private static DiscInput? DetectReadable(string path)
    {
        if (File.Exists(path))
        {
            using var stream = File.OpenRead(path);
            GdfReader.Open(stream);
            return new DiscInput(DiscInputKind.Iso, path, null);
        }
        if (!Directory.Exists(path)) return null;
        if (GodContainer.Find(path) is { } god)
            return new DiscInput(DiscInputKind.God, path, god.Header.MediaId.ToString("X8"));
        if (File.Exists(System.IO.Path.Combine(path, "default.xex"))) return new DiscInput(DiscInputKind.ExtractedFolder, path, null);
        return EnumeratePackages(path).Any() ? new DiscInput(DiscInputKind.PackageFolder, path, null) : null;
    }

    public IDiscFiles OpenFiles()
    {
        switch (Kind)
        {
            case DiscInputKind.Iso:
                var stream = File.OpenRead(Path);
                try { return new GdfDiscFiles(GdfReader.Open(stream)); }
                catch { stream.Dispose(); throw; }
            case DiscInputKind.God:
                var container = GodContainer.Find(Path) ?? throw new InvalidDataException($"{Path} no longer holds a GOD container.");
                return new GdfDiscFiles(container.OpenGdf());
            case DiscInputKind.ExtractedFolder:
                return new FolderDiscFiles(Path);
            default:
                throw new InvalidOperationException("A package folder holds no disc file system.");
        }
    }

    public static IEnumerable<(string Path, PackageHeader Header)> EnumeratePackages(string folder) =>
        Directory.EnumerateFiles(folder, "*", Everything)
            .Select(f => (Path: f, Header: PackageHeader.TryRead(f)))
            .Where(p => p.Header is { TitleId: PackageHeader.Fm4TitleId, ContentType: PackageHeader.MarketplaceContent })
            .Select(p => (p.Path, p.Header!));
}
