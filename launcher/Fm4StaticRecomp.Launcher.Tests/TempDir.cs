// Copyright (c) 2026, i2kiselev
// BSD 3-Clause License: see LICENSE in the repository root.
namespace Fm4StaticRecomp.Launcher.Tests;

public sealed class TempDir : IDisposable
{
    public string Path { get; } = System.IO.Path.Combine(System.IO.Path.GetTempPath(),
        "fm4-launcher-tests", Guid.NewGuid().ToString("N"));

    public TempDir() => Directory.CreateDirectory(Path);

    public string File(string relative, byte[]? content = null)
    {
        var full = System.IO.Path.Combine(Path, relative.Replace('\\', System.IO.Path.DirectorySeparatorChar));
        Directory.CreateDirectory(System.IO.Path.GetDirectoryName(full)!);
        if (content is not null) System.IO.File.WriteAllBytes(full, content);
        return full;
    }

    public void Dispose()
    {
        try
        {
            // A recursive delete refuses a junction (access denied); links go first, on their own.
            var options = new EnumerationOptions { RecurseSubdirectories = true, AttributesToSkip = 0 };
            foreach (var link in Directory.EnumerateDirectories(Path, "*", options)
                         .Where(d => new DirectoryInfo(d).LinkTarget is not null).OrderByDescending(d => d.Length).ToList())
                Directory.Delete(link);
            Directory.Delete(Path, recursive: true);
        }
        catch (IOException) { }
    }
}
