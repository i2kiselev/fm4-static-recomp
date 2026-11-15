// Copyright (c) 2026, i2kiselev
// BSD 3-Clause License: see LICENSE in the repository root.
using System.Buffers.Binary;
using System.Text;
using Fm4StaticRecomp.Launcher.Core.Content;
using Fm4StaticRecomp.Launcher.Core.Install;
using Fm4StaticRecomp.Launcher.Core.Platform;
using Fm4StaticRecomp.Launcher.Core.Profiles;

namespace Fm4StaticRecomp.Launcher.Tests.Profiles;

public class XboxSaveTests
{
    private sealed record Entry(string Name, short Parent, byte[]? Data, int[]? Blocks = null);

    private const ulong ProfileId = 0xE000027DD85E4073;

    // A small console-signed save: one file-table block (block 0), data from block 1 on unless an entry
    // names its blocks, which are then chained through the hash table instead of being consecutive.
    private static byte[] Container(IReadOnlyList<Entry> entries, string magic = "CON ", uint title = 0x4D530910,
        uint type = 1, ulong profile = ProfileId, byte separation = 2)
    {
        const int headerSize = 0x971A;
        const int firstTable = 0xA000;
        const int end = 0xFFFFFF;
        var blocks = new Dictionary<int, byte[]> { [0] = new byte[0x1000] };
        var next = new Dictionary<int, int> { [0] = end };
        var free = 1;
        for (var i = 0; i < entries.Count; i++)
        {
            var entry = entries[i];
            var at = blocks[0].AsSpan(i * 0x40, 0x40);
            Encoding.ASCII.GetBytes(entry.Name, at);
            var count = entry.Data is null ? 0 : (entry.Data.Length + 0xFFF) / 0x1000;
            var list = entry.Blocks ?? Enumerable.Range(free, count).ToArray();
            if (entry.Blocks is null) free += count;
            at[0x28] = (byte)(entry.Name.Length | (entry.Data is null ? 0x80 : 0) | (entry.Blocks is null ? 0x40 : 0));
            var start = list.Length > 0 ? list[0] : 0;
            at[0x2F] = (byte)start;
            at[0x30] = (byte)(start >> 8);
            at[0x31] = (byte)(start >> 16);
            BinaryPrimitives.WriteInt16BigEndian(at[0x32..], entry.Parent);
            if (entry.Data is null) continue;
            BinaryPrimitives.WriteUInt32BigEndian(at[0x34..], (uint)entry.Data.Length);
            for (var k = 0; k < list.Length; k++)
            {
                var block = new byte[0x1000];
                entry.Data.AsSpan(k * 0x1000, Math.Min(0x1000, entry.Data.Length - k * 0x1000)).CopyTo(block);
                blocks[list[k]] = block;
                next[list[k]] = k + 1 < list.Length ? list[k + 1] : end;
            }
        }
        var allocated = blocks.Keys.Max() + 1;
        // Below 0xAA blocks the two level-0 hash tables come first, then data block n.
        var file = new byte[firstTable + (2 + allocated) * 0x1000];
        Encoding.ASCII.GetBytes(magic, file);
        BinaryPrimitives.WriteUInt32BigEndian(file.AsSpan(0x340), headerSize);
        BinaryPrimitives.WriteUInt32BigEndian(file.AsSpan(0x344), type);
        BinaryPrimitives.WriteUInt32BigEndian(file.AsSpan(0x360), title);
        BinaryPrimitives.WriteUInt64BigEndian(file.AsSpan(0x371), profile);
        file[0x379] = 0x24;
        file[0x37B] = separation;
        file[0x37C] = 1;
        BinaryPrimitives.WriteUInt32BigEndian(file.AsSpan(0x395), (uint)allocated);
        Encoding.BigEndianUnicode.GetBytes("Racer", file.AsSpan(0x411));
        var table = firstTable + ((separation & 2) << 11);
        foreach (var (block, data) in blocks)
        {
            data.CopyTo(file, firstTable + (2 + block) * 0x1000);
            var link = next[block];
            file[table + block * 0x18 + 0x15] = (byte)(link >> 16);
            file[table + block * 0x18 + 0x16] = (byte)(link >> 8);
            file[table + block * 0x18 + 0x17] = (byte)link;
        }
        return file;
    }

    private static byte[] Bytes(int length, int seed) =>
        Enumerable.Range(0, length).Select(i => (byte)(i * 7 + seed)).ToArray();

    private static string Save(TempDir temp, byte[] container) => temp.File(@"in\ForzaProfile", container);

    [Fact]
    public void Extracts_the_files_and_folders_under_the_profile_id_with_the_header()
    {
        using var temp = new TempDir();
        var profile = Bytes(5000, 1);
        var livery = Bytes(300, 2);
        var source = Save(temp, Container([
            new Entry("ForzaProfile", -1, profile),
            new Entry("Liveries", -1, null),
            new Entry("100.liv", 1, livery),
        ]));
        var out_ = Path.Combine(temp.Path, "out");
        var save = XboxSave.Extract(source, out_);
        Assert.Equal(new XboxSaveInfo("E000027DD85E4073", "Racer"), save);
        var content = Path.Combine(out_, "E000027DD85E4073", "4D530910", "00000001", "ForzaProfile");
        Assert.Equal(profile, File.ReadAllBytes(Path.Combine(content, "ForzaProfile")));
        Assert.Equal(livery, File.ReadAllBytes(Path.Combine(content, "Liveries", "100.liv")));
        Assert.Equal(ContentHeader.Build(ContentHeader.SaveGame, "Racer", "ForzaProfile", null),
            File.ReadAllBytes(Path.Combine(out_, "E000027DD85E4073", "4D530910", "Headers", "00000001", "ForzaProfile.header")));
    }

    [Fact]
    public void Follows_a_file_s_block_chain_in_the_active_hash_table()
    {
        using var temp = new TempDir();
        var profile = Bytes(9000, 3);
        foreach (var separation in new byte[] { 0, 2 })
        {
            var source = Save(temp, Container([new Entry("ForzaProfile", -1, profile, [3, 1, 2])], separation: separation));
            var out_ = Path.Combine(temp.Path, $"out{separation}");
            XboxSave.Extract(source, out_);
            Assert.Equal(profile, File.ReadAllBytes(
                Path.Combine(out_, "E000027DD85E4073", "4D530910", "00000001", "ForzaProfile", "ForzaProfile")));
        }
    }

    [Fact]
    public void Import_of_the_extracted_save_signs_in_as_its_xbox_profile()
    {
        using var temp = new TempDir();
        var layout = new InstallLayout(Path.Combine(temp.Path, "install"));
        Directory.CreateDirectory(layout.Content);
        var store = new ProfileStore(layout, Platforms.Current, "vanilla");
        var source = Save(temp, Container([new Entry("ForzaProfile", -1, Bytes(100, 4))]));
        var out_ = Path.Combine(temp.Path, "out");
        var save = XboxSave.Extract(source, out_);
        var profile = store.Import(Path.Combine(out_, save.Xuid), save.DisplayName, "vanilla");
        Assert.Equal("E000027DD85E4073", profile.Xuid);
        Assert.True(File.Exists(Path.Combine(profile.User, "E000027DD85E4073", "4D530910", "00000001", "ForzaProfile", "ForzaProfile")));
    }

    public static TheoryData<string, byte[]> Refused => new()
    {
        { "is not an Xbox 360 save file", Container([new Entry("ForzaProfile", -1, [1])], magic: "LIVE") },
        { "not of Forza Motorsport 4", Container([new Entry("ForzaProfile", -1, [1])], title: 0x4D5307E6) },
        { "but not a save", Container([new Entry("ForzaProfile", -1, [1])], type: 2) },
        { "Xbox profile", Container([new Entry("ForzaProfile", -1, [1])], profile: 0) },
        { "not the career", Container([new Entry("Other", -1, [1])]) },
        { "is damaged", Container([new Entry("ForzaProfile", -1, [1]), new Entry("..", -1, [2])]) },
    };

    [Theory]
    [MemberData(nameof(Refused))]
    public void Refuses_what_is_not_an_intact_fm4_career_save(string message, byte[] container)
    {
        using var temp = new TempDir();
        var out_ = Path.Combine(temp.Path, "out");
        var error = Assert.ThrowsAny<IOException>(() => XboxSave.Extract(Save(temp, container), out_));
        Assert.Contains(message, error.Message);
        Assert.False(Directory.Exists(Path.Combine(out_, "E000027DD85E4073", "4D530910", "00000001")));
    }

    [Fact]
    public void Refuses_a_file_too_short_to_be_a_container()
    {
        using var temp = new TempDir();
        var source = temp.File(@"in\short", Encoding.ASCII.GetBytes("CON nothing"));
        Assert.Contains("is not an Xbox 360 save file",
            Assert.ThrowsAny<IOException>(() => XboxSave.Extract(source, Path.Combine(temp.Path, "out"))).Message);
    }
}
