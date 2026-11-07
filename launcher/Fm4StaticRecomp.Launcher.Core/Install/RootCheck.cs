// Copyright (c) 2026, i2kiselev
// BSD 3-Clause License: see LICENSE in the repository root.
using Fm4StaticRecomp.Launcher.Core.Platform;

namespace Fm4StaticRecomp.Launcher.Core.Install;

// CreateLink makes a directory link (the profiles' content link); null skips that probe.
public sealed record RootEnvironment(IReadOnlyList<string> Protected, string Temp, Action<string, string>? CreateLink = null, bool WindowsPathLimits = true)
{
    public static RootEnvironment Current => new(
        new[]
        {
            Environment.GetFolderPath(Environment.SpecialFolder.ProgramFiles),
            Environment.GetFolderPath(Environment.SpecialFolder.ProgramFilesX86),
            Environment.GetFolderPath(Environment.SpecialFolder.Windows),
        }.Where(p => p.Length > 0).ToList(),
        Path.GetTempPath(),
        Platforms.Current.CreateDirectoryLink, OperatingSystem.IsWindows());
}

public static class RootCheck
{
    // 260 minus the longest path a payload build creates below the root (196) minus a margin of 10.
    public const int MaximumLength = 54;

    // The same texts as setup's Assert-Fm4ToolchainPathSupported (tools\release-common.ps1).
    private const string Brackets = "CMake, which the build needs, finds no compiler when its folder path holds [ or ]. " +
                                    "Move the installation to a folder whose path has no square brackets.";
    private const string NonEnglishParentheses = "CMake, which the build needs, cannot run its link commands from a path that " +
                                                 "holds both non-English letters and parentheses. Move the installation to a " +
                                                 "folder whose path has no parentheses.";

    public static string? Problem(string root, RootEnvironment environment)
    {
        var full = Path.TrimEndingDirectorySeparator(Path.GetFullPath(root));
        return Reason(full, environment) switch
        {
            null => null,
            // Setup's texts say where to move already.
            (Brackets or NonEnglishParentheses) and var own => $"The launcher cannot install into {root}: {own}",
            var reason when full.Length > MaximumLength =>
                $"The launcher cannot install into {root}: {reason} Move the launcher's folder to a short path such as " +
                $"{Path.GetPathRoot(full)}FM4 and start it again.",
            var reason => $"The launcher cannot install into {root}: {reason} Move the launcher's folder and start it again.",
        };
    }

    private static string? Reason(string root, RootEnvironment environment)
    {
        foreach (var folder in environment.Protected)
            if (IsUnder(root, folder)) return $"it is inside {folder}, which Windows protects.";
        if (IsUnder(root, environment.Temp))
            return "it is in the temporary folder, which Windows clears (extract the launcher's zip first).";
        if (root.IndexOfAny(['[', ']']) >= 0) return Brackets;
        if (environment.WindowsPathLimits && root.Any(c => c > 0x7F) && root.IndexOfAny(['(', ')']) >= 0) return NonEnglishParentheses;
        if (environment.WindowsPathLimits && root.Length > MaximumLength)
            return $"the path is {root.Length} characters long; the build needs one of at most {MaximumLength}.";
        try
        {
            Directory.CreateDirectory(root);
            var probe = Path.Combine(root, $".write-test-{Guid.NewGuid():N}");
            File.WriteAllBytes(probe, []);
            File.Delete(probe);
        }
        catch (Exception ex) when (ex is UnauthorizedAccessException or IOException)
        {
            return $"the folder cannot be written ({ex.Message}).";
        }
        if (environment.CreateLink is { } createLink)
        {
            // Found here, not after a 40-minute build at the first profile or content install.
            var link = Path.Combine(root, $".link-test-{Guid.NewGuid():N}");
            try { createLink(link, root); }
            catch (Exception ex) when (ex is UnauthorizedAccessException or IOException)
            {
                return "its drive cannot hold the folder links the profiles share their content through " +
                       $"({ex.Message}); an NTFS drive can, FAT32, exFAT and network drives cannot.";
            }
            finally
            {
                if (Directory.Exists(link)) Directory.Delete(link);
            }
        }
        return null;
    }

    private static bool IsUnder(string path, string folder)
    {
        var prefix = Path.TrimEndingDirectorySeparator(Path.GetFullPath(folder)) + Path.DirectorySeparatorChar;
        return (path + Path.DirectorySeparatorChar).StartsWith(prefix, StringComparison.OrdinalIgnoreCase);
    }
}
