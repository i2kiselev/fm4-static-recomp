// Copyright (c) 2026, i2kiselev
// BSD 3-Clause License: see LICENSE in the repository root.
using System.Runtime.InteropServices;
using System.Text;

namespace Fm4StaticRecomp.Launcher.Core.Platform;

// Resolve selected sources before inspecting their type or deriving an Xbox XUID
// from the folder name. Finder alias files are not Unix symbolic links.
public static class SourcePath
{
    public static string Resolve(string path)
    {
        path = Path.TrimEndingDirectorySeparator(Path.GetFullPath(path));
        for (var depth = 0; depth < 32; depth++)
        {
            FileSystemInfo entry = Directory.Exists(path) ? new DirectoryInfo(path) : new FileInfo(path);
            if (entry.LinkTarget is not null)
            {
                path = entry.ResolveLinkTarget(returnFinalTarget: true)?.FullName
                       ?? throw new IOException($"The shortcut {path} has no reachable target.");
                continue;
            }
            var resolved = OperatingSystem.IsMacOS() ? MacAlias.Resolve(path) : path;
            if (resolved == path) return path;
            path = resolved;
        }
        throw new IOException($"The shortcut {path} contains too many links.");
    }

    private static class MacAlias
    {
        private const string Framework = "/System/Library/Frameworks/CoreFoundation.framework/CoreFoundation";
        private static readonly nint Library = NativeLibrary.Load(Framework);
        private static readonly nint AliasKey = Marshal.ReadIntPtr(NativeLibrary.GetExport(Library, "kCFURLIsAliasFileKey"));

        public static string Resolve(string path)
        {
            var bytes = Encoding.UTF8.GetBytes(path);
            var url = CFURLCreateFromFileSystemRepresentation(0, bytes, bytes.Length, 0);
            if (url == 0) throw new IOException($"Cannot read the source path {path}.");
            nint alias = 0, data = 0, target = 0;
            try
            {
                if (CFURLCopyResourcePropertyForKey(url, AliasKey, out alias, 0) == 0 || alias == 0 || CFBooleanGetValue(alias) == 0)
                    return path;
                data = CFURLCreateBookmarkDataFromFile(0, url, 0);
                if (data == 0) throw new IOException($"The Finder alias {path} cannot be read.");
                // Resolve without UI or mounting a disconnected volume.
                target = CFURLCreateByResolvingBookmarkData(0, data, (1u << 8) | (1u << 9), 0, 0, out _, 0);
                var buffer = new byte[32768];
                if (target == 0 || CFURLGetFileSystemRepresentation(target, 1, buffer, buffer.Length) == 0)
                    throw new IOException($"The target of Finder alias {path} is unavailable. Connect its drive and choose it again.");
                return Encoding.UTF8.GetString(buffer, 0, Array.IndexOf(buffer, (byte)0));
            }
            finally
            {
                if (target != 0) CFRelease(target);
                if (data != 0) CFRelease(data);
                if (alias != 0) CFRelease(alias);
                CFRelease(url);
            }
        }

        [DllImport(Framework)] private static extern nint CFURLCreateFromFileSystemRepresentation(nint allocator, byte[] bytes, nint length, byte directory);
        [DllImport(Framework)] private static extern byte CFURLCopyResourcePropertyForKey(nint url, nint key, out nint value, nint error);
        [DllImport(Framework)] private static extern byte CFBooleanGetValue(nint value);
        [DllImport(Framework)] private static extern nint CFURLCreateBookmarkDataFromFile(nint allocator, nint url, nint error);
        [DllImport(Framework)] private static extern nint CFURLCreateByResolvingBookmarkData(nint allocator, nint data, nuint options, nint relative, nint properties, out byte stale, nint error);
        [DllImport(Framework)] private static extern byte CFURLGetFileSystemRepresentation(nint url, byte resolve, byte[] buffer, nint length);
        [DllImport(Framework)] private static extern void CFRelease(nint value);
    }
}
