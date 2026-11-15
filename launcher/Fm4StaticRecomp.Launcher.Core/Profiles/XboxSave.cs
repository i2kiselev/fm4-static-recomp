// Copyright (c) 2026, i2kiselev
// BSD 3-Clause License: see LICENSE in the repository root.
using System.Buffers.Binary;
using System.Text;
using Fm4StaticRecomp.Launcher.Core.Content;

namespace Fm4StaticRecomp.Launcher.Core.Profiles;

public sealed record XboxSaveInfo(string Xuid, string DisplayName);

// An FM4 save copied off an Xbox 360 (the file on a USB drive, or one exported with Horizon or Velocity) is one
// STFS container. The game encrypts the save with keys derived from the XUID of the Xbox profile that made it,
// and only the container's header names that profile: the files extracted from it load only under that XUID.
public static class XboxSave
{
    public const string ContentName = "ForzaProfile";
    private const uint TitleId = 0x4D530910;
    private const int MinimumLength = 0x1800;
    // 0x70E4 data blocks with their hash tables, which is as large as Volume reads.
    private const long MaximumLength = 128L << 20;
    private static readonly char[] ForbiddenInNames = ['/', '\\', ':', '*', '?', '"', '<', '>', '|'];

    private sealed record Entry(string Name, bool Directory, bool Consecutive, int Start, int Parent, int Size);

    // Unpacks the container as its Xbox profile's user folder, <destination>\<xuid>\4D530910\..., with the
    // header the SDK reads. Nothing is written for a file it refuses.
    public static XboxSaveInfo Extract(string containerPath, string destination)
    {
        var name = Path.GetFileName(containerPath);
        // A disc image chosen by mistake is not read into memory.
        if (new FileInfo(containerPath).Length is < MinimumLength or > MaximumLength)
            throw new IOException($"{name} is not an Xbox 360 save file.");
        var data = File.ReadAllBytes(containerPath);
        if (Encoding.ASCII.GetString(data, 0, 4) != "CON ")
            throw new IOException($"{name} is not an Xbox 360 save file.");
        if (BinaryPrimitives.ReadUInt32BigEndian(data.AsSpan(0x360)) != TitleId)
            throw new IOException($"{name} is a save of {Text(data, 0x1691)}, not of Forza Motorsport 4.");
        if (BinaryPrimitives.ReadUInt32BigEndian(data.AsSpan(0x344)) != ContentHeader.SaveGame)
            throw new IOException($"{name} is Forza Motorsport 4 content, but not a save.");
        var profile = BinaryPrimitives.ReadUInt64BigEndian(data.AsSpan(0x371));
        // The SDK signs in with none of these (see its user_xuid check).
        if (profile == 0 || (profile & 0x00C0000000000000) != 0)
            throw new IOException($"{name} does not name an Xbox profile the game can sign in as, so its save cannot be read.");

        var volume = new Volume(data, name);
        var entries = volume.Entries();
        var paths = entries.Select((_, i) => PathOf(entries, i, name)).ToList();
        if (!entries.Any(e => e is { Parent: -1, Directory: false, Name: ContentName }))
            throw new IOException($"{name} is a Forza Motorsport 4 save, but not the career (it has no {ContentName} file).");

        var xuid = profile.ToString("X16");
        var title = Path.Combine(destination, xuid, "4D530910");
        var content = Path.Combine(title, "00000001", ContentName);
        Directory.CreateDirectory(content);
        for (var i = 0; i < entries.Count; i++)
        {
            var target = Path.Combine(content, paths[i]);
            if (entries[i].Directory) Directory.CreateDirectory(target);
            else
            {
                Directory.CreateDirectory(Path.GetDirectoryName(target)!);
                File.WriteAllBytes(target, volume.Read(entries[i]));
            }
        }
        var headers = Path.Combine(title, "Headers", "00000001");
        Directory.CreateDirectory(headers);
        File.WriteAllBytes(Path.Combine(headers, ContentName + ".header"),
            ContentHeader.FromContainer(data, ContentHeader.SaveGame, ContentName)!);
        return new XboxSaveInfo(xuid, Text(data, 0x411));
    }

    private static string Text(byte[] data, int offset) =>
        Encoding.BigEndianUnicode.GetString(data, offset, 0x80).Split('\0')[0];

    private static string PathOf(IReadOnlyList<Entry> entries, int index, string name)
    {
        var parts = new List<string>();
        for (var i = index; i != -1; i = entries[i].Parent)
        {
            if (parts.Count == entries.Count || i < 0 || i >= entries.Count || !entries[i].Directory && i != index ||
                entries[i].Name is "." or ".." || entries[i].Name.IndexOfAny(ForbiddenInNames) >= 0 ||
                entries[i].Name.Any(char.IsControl))
                throw new IOException($"{name} is damaged: its file list is not readable.");
            parts.Insert(0, entries[i].Name);
        }
        return Path.Combine([.. parts]);
    }

    // STFS as the console writes it: 4 KB blocks behind the header, hash tables between them (two copies of
    // each when the separation's low bit is clear, the active one chosen by the level above), each block's hash
    // entry holding its successor. FM4 saves stay below 0x70E4 blocks, so at most one level sits above.
    private sealed class Volume
    {
        private const int BlockSize = 0x1000;
        private const int EntrySize = 0x40;
        private const int HashEntrySize = 0x18;
        private const int PerTable = 0xAA;
        private const int PerLevel1 = 0x70E4;

        private readonly byte[] _data;
        private readonly string _name;
        private readonly long _firstTable;
        private readonly int _separation;
        private readonly int _shift;
        private readonly int _step;
        private readonly bool _twoLevels;
        private readonly int _tableBlocks;
        private readonly int _tableStart;

        public Volume(byte[] data, string name)
        {
            _data = data;
            _name = name;
            _firstTable = (BinaryPrimitives.ReadUInt32BigEndian(data.AsSpan(0x340)) + 0xFFFL) & ~0xFFFL;
            _separation = data[0x37B];
            _shift = ~_separation & 1;
            // Blocks per level-0 group: its hash table copies and 0xAA data blocks.
            _step = PerTable + (1 << _shift);
            _tableBlocks = data[0x37C] | data[0x37D] << 8;
            _tableStart = data[0x37E] | data[0x37F] << 8 | data[0x380] << 16;
            var allocated = BinaryPrimitives.ReadUInt32BigEndian(data.AsSpan(0x395));
            if (data[0x379] != 0x24 || BinaryPrimitives.ReadUInt32BigEndian(data.AsSpan(0x3A9)) != 0 || allocated > PerLevel1)
                throw Damaged();
            _twoLevels = allocated > PerTable;
        }

        public List<Entry> Entries()
        {
            var entries = new List<Entry>();
            foreach (var block in Chain(_tableStart, _tableBlocks, consecutive: false))
                for (var i = 0; i < BlockSize / EntrySize; i++)
                {
                    var at = At(DataAddress(block) + i * EntrySize, EntrySize);
                    var length = at[0x28] & 0x3F;
                    if (length == 0) return entries;
                    entries.Add(new Entry(Encoding.ASCII.GetString(at[..length]), (at[0x28] & 0x80) != 0,
                        (at[0x28] & 0x40) != 0, at[0x2F] | at[0x30] << 8 | at[0x31] << 16,
                        BinaryPrimitives.ReadInt16BigEndian(at[0x32..]), (int)BinaryPrimitives.ReadUInt32BigEndian(at[0x34..])));
                }
            return entries;
        }

        public byte[] Read(Entry entry)
        {
            if (entry.Size < 0) throw Damaged();
            var bytes = new byte[entry.Size];
            var offset = 0;
            foreach (var block in Chain(entry.Start, (entry.Size + BlockSize - 1) / BlockSize, entry.Consecutive))
            {
                var length = Math.Min(BlockSize, entry.Size - offset);
                At(DataAddress(block), length).CopyTo(bytes.AsSpan(offset));
                offset += length;
            }
            return bytes;
        }

        private IEnumerable<int> Chain(int start, int count, bool consecutive)
        {
            var block = start;
            for (var i = 0; i < count; i++)
            {
                yield return block;
                if (consecutive) block++;
                else
                {
                    var link = At(HashEntryAddress(block) + 0x15, 3);
                    block = link[0] << 16 | link[1] << 8 | link[2];
                }
            }
        }

        private long DataAddress(int block)
        {
            if (block >= PerLevel1) throw Damaged();
            long backing = (((block + PerTable) / PerTable) << _shift) + block;
            if (block >= PerTable) backing += ((block + PerLevel1) / PerLevel1) << _shift;
            return _firstTable + (backing << 12);
        }

        private long HashEntryAddress(int block)
        {
            if (block >= PerLevel1) throw Damaged();
            long table = block < PerTable ? 0 : (long)(block / PerTable) * _step + (1 << _shift);
            var address = _firstTable + (table << 12) + block % PerTable * HashEntrySize;
            if (!_twoLevels) return address + ((_separation & 2) << 11);
            var top = _firstTable + ((long)_step << 12) + ((_separation & 2) << 11);
            return address + ((At(top + block / PerTable * HashEntrySize + 0x14, 1)[0] & 0x40) << 6);
        }

        private ReadOnlySpan<byte> At(long address, int length) =>
            address < 0 || address + length > _data.Length ? throw Damaged() : _data.AsSpan((int)address, length);

        private IOException Damaged() => new($"{_name} is damaged: its file list is not readable.");
    }
}
