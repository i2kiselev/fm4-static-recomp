// Copyright (c) 2026, i2kiselev
// BSD 3-Clause License: see LICENSE in the repository root.
namespace Fm4StaticRecomp.Launcher.Tests;

// Disc and mod manifests use Windows separators even when exercised on Unix.
internal static class TestPath
{
    public static string Combine(params string[] parts) =>
        Path.Combine(parts.Select(p => p.Replace('\\', Path.DirectorySeparatorChar)).ToArray());
    public static void CreateHardLink(string path, string existing)
    {
        if (OperatingSystem.IsWindows()) Assert.True(CreateHardLinkWindows(path, existing, IntPtr.Zero));
        else Assert.Equal(0, Link(existing, path));
    }

    [System.Runtime.InteropServices.DllImport("kernel32.dll", EntryPoint = "CreateHardLinkW", CharSet = System.Runtime.InteropServices.CharSet.Unicode)]
    private static extern bool CreateHardLinkWindows(string path, string existing, IntPtr securityAttributes);

    [System.Runtime.InteropServices.DllImport("libc", EntryPoint = "link")]
    private static extern int Link(string existing, string path);
}
