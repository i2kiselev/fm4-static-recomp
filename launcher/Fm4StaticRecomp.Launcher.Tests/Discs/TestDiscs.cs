using System.Buffers.Binary;
using System.Security.Cryptography;
using System.Text.Json;

namespace Fm4StaticRecomp.Launcher.Tests.Discs;

public static class TestDiscs
{
    public const uint PalMediaId = 0x61D62D9F;
    public const uint UsaMediaId = 0x26FF1101;
    public const string PalName = "1.0 PAL (Europe, Australia)";
    public const string UsaName = "1.0 USA";
    public const uint Rev1EuropeMediaId = 0x42C67824;
    public const uint Rev1UsaMediaId = 0x33E7B39F;
    public const string Rev1EuropeName = "Rev 1 Europe";
    public const string Rev1UsaName = "Rev 1 USA";

    public static byte[] Xex(uint mediaId, int extra = 0)
    {
        var xex = new byte[0x40 + extra];
        "XEX2"u8.CopyTo(xex);
        BinaryPrimitives.WriteUInt32BigEndian(xex.AsSpan(0x14), 1);
        BinaryPrimitives.WriteUInt32BigEndian(xex.AsSpan(0x18), 0x00040006);
        BinaryPrimitives.WriteUInt32BigEndian(xex.AsSpan(0x1C), 0x30);
        BinaryPrimitives.WriteUInt32BigEndian(xex.AsSpan(0x30), mediaId);
        for (var i = 0x40; i < xex.Length; i++) xex[i] = (byte)i;
        return xex;
    }

    public static Dictionary<string, byte[]> Pal() => new()
    {
        ["default.xex"] = Xex(PalMediaId),
        ["Media/a.bin"] = [5, 6],
        ["Media/b.bin"] = [7],
        ["$SystemUpdate/su.bin"] = [8],
    };

    // Rev 1: its own executable, the shared files under a folder renamed by case, and one file more.
    public static Dictionary<string, byte[]> Rev1Europe() => new()
    {
        ["default.xex"] = Xex(Rev1EuropeMediaId),
        ["MEDIA/a.bin"] = [5, 6],
        ["MEDIA/b.bin"] = [7],
        ["MEDIA/patch.bin"] = [9],
        ["$SystemUpdate/su.bin"] = [8],
    };

    public static string Sha(byte[] data) => Convert.ToHexString(SHA256.HashData(data));

    public static string WriteManifest(TempDir temp, IReadOnlyDictionary<string, byte[]> pal,
        IReadOnlyDictionary<string, byte[]>? packages = null)
    {
        object Entry(string path, byte[] data) => path.StartsWith("$SystemUpdate/", StringComparison.Ordinal)
            ? new { path, size = data.LongLength, sha256 = Sha(data), optional = true }
            : new { path, size = data.LongLength, sha256 = Sha(data) };
        var manifest = new
        {
            schema_version = 2,
            title_id = "4D530910",
            disc1 = new
            {
                releases = new object[]
                {
                    new { name = PalName, media_id = PalMediaId.ToString("X8"), files = new[] { Entry("default.xex", pal["default.xex"]) } },
                    new { name = UsaName, media_id = UsaMediaId.ToString("X8"), files = new[] { Entry("default.xex", Xex(UsaMediaId)) } },
                    new
                    {
                        name = Rev1EuropeName, media_id = Rev1EuropeMediaId.ToString("X8"),
                        files = new[] { Entry("default.xex", Xex(Rev1EuropeMediaId)), Entry("Media/patch.bin", [9]) },
                    },
                    new
                    {
                        name = Rev1UsaName, media_id = Rev1UsaMediaId.ToString("X8"),
                        files = new[] { Entry("default.xex", Xex(Rev1UsaMediaId)), Entry("Media/patch.bin", [9]) },
                    },
                },
                files = pal.Where(f => f.Key != "default.xex").OrderBy(f => f.Key, StringComparer.Ordinal)
                    .Select(f => Entry(f.Key, f.Value)).ToArray(),
            },
            disc2 = new
            {
                packages = (packages ?? new Dictionary<string, byte[]>())
                    .Select(p => new { name = p.Key, size = p.Value.LongLength, sha256 = Sha(p.Value) }).ToArray(),
            },
        };
        return temp.File("supported-discs.json", JsonSerializer.SerializeToUtf8Bytes(manifest));
    }
}
