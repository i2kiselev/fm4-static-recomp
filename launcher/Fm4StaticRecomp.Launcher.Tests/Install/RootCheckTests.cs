// Copyright (c) 2026, i2kiselev
// BSD 3-Clause License: see LICENSE in the repository root.
using System.Runtime.InteropServices;
using System.Text;
using Fm4StaticRecomp.Launcher.Core.Install;
using Fm4StaticRecomp.Launcher.Core.Platform;

namespace Fm4StaticRecomp.Launcher.Tests.Install;

public class RootCheckTests
{
    private static readonly RootEnvironment Env = new([@"C:\Program Files", @"C:\Program Files (x86)", @"C:\Windows"], @"C:\Users\a\AppData\Local\Temp\");

    [Theory]
    [InlineData(@"C:\Program Files\FM4\data", "Program Files", " Move the launcher's folder and start it again.")]
    [InlineData(@"C:\Users\a\AppData\Local\Temp\Rar$EX01\data", "temporary", " Move the launcher's folder and start it again.")]
    [InlineData(@"C:\Games\FM4 [1]\data", "[ or ]", " Move the installation to a folder whose path has no square brackets.")]
    [InlineData(@"C:\Игры (1)\data", "non-English", " Move the installation to a folder whose path has no parentheses.")]
    [InlineData(@"D:\Games\aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\data", "54",
        @" Move the launcher's folder to a short path such as D:\FM4 and start it again.")]
    public void Refuses_with_the_reason_and_one_way_out(string root, string reason, string advice)
    {
        if (!OperatingSystem.IsWindows()) return;
        var problem = RootCheck.Problem(root, Env)!;
        Assert.Contains(reason, problem);
        Assert.StartsWith($"The launcher cannot install into {root}: ", problem);
        Assert.EndsWith(advice, problem);
        Assert.Single(System.Text.RegularExpressions.Regex.Matches(problem, @"\bMove\b"));
    }

    [Fact]
    public void Uses_setup_s_own_texts_for_the_cmake_limits()
    {
        Assert.Contains("CMake, which the build needs, finds no compiler when its folder path holds [ or ]. Move the installation to a folder whose path has no square brackets.",
            RootCheck.Problem(@"C:\G[1]\data", Env)!);
        Assert.Contains("CMake, which the build needs, cannot run its link commands from a path that holds both non-English letters and parentheses. Move the installation to a folder whose path has no parentheses.",
            RootCheck.Problem(@"C:\Игры (1)\data", Env)!);
    }

    [Fact]
    public void Accepts_cyrillic_spaces_and_english_parentheses()
    {
        // Not under TempDir: the test runner's temp folder is %TEMP% and too long.
        var parent = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.UserProfile), "fm4t-" + Guid.NewGuid().ToString("N")[..6]);
        try
        {
            foreach (var name in new[] { "Тест 1", "Games (1)" })
                Assert.Null(RootCheck.Problem(Path.Combine(parent, name, "data"), Env));
        }
        finally { Directory.Delete(parent, recursive: true); }
    }

    [Fact]
    public void Sees_the_temporary_folder_through_its_short_name()
    {
        if (!OperatingSystem.IsWindows()) return;
        var parent = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.UserProfile), "fm4t long name " + Guid.NewGuid().ToString("N")[..6]);
        Directory.CreateDirectory(parent);
        try
        {
            var shortName = new StringBuilder(260);
            GetShortPathNameW(parent, shortName, shortName.Capacity);
            if (shortName.ToString() is var brief && (brief.Length == 0 || brief == parent)) return;
            Assert.Contains("temporary", RootCheck.Problem(Path.Combine(parent, "data"), Env with { Temp = brief })!);
            Assert.Contains("temporary", RootCheck.Problem(Path.Combine(brief, "data"), Env with { Temp = parent })!);
        }
        finally { Directory.Delete(parent, recursive: true); }
    }

    [Fact]
    public void Refuses_a_drive_that_cannot_hold_the_content_links()
    {
        // exFAT and FAT32 drives and network shares hold no junctions, and every profile needs one.
        var parent = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.UserProfile), "fm4t-" + Guid.NewGuid().ToString("N")[..6]);
        try
        {
            var root = Path.Combine(parent, "data");
            var problem = RootCheck.Problem(root, Env with { CreateLink = (_, _) => throw new IOException("not supported") })!;
            Assert.Contains("folder links", problem);
            Assert.Empty(Directory.GetFileSystemEntries(root));
        }
        finally { Directory.Delete(parent, recursive: true); }
    }

    [Fact]
    public void Accepts_a_drive_that_holds_links_and_leaves_no_probe()
    {
        var parent = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.UserProfile), "fm4t-" + Guid.NewGuid().ToString("N")[..6]);
        try
        {
            var root = Path.Combine(parent, "data");
            Assert.Null(RootCheck.Problem(root, Env with { CreateLink = Platforms.Current.CreateDirectoryLink }));
            Assert.Empty(Directory.GetFileSystemEntries(root));
        }
        finally { Directory.Delete(parent, recursive: true); }
    }

    [DllImport("kernel32.dll", CharSet = CharSet.Unicode)]
    private static extern uint GetShortPathNameW(string longPath, StringBuilder shortPath, int length);

    [Fact]
    public void Refuses_a_folder_it_cannot_write()
    {
        if (!OperatingSystem.IsWindows()) return;
        Assert.NotNull(RootCheck.Problem(@"C:\Windows\System32\fm4-test\data", Env with { Protected = [] }));
    }
}
