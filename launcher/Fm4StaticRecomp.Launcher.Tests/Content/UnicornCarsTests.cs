// Copyright (c) 2026, i2kiselev
// BSD 3-Clause License: see LICENSE in the repository root.
using System.Buffers.Binary;
using Fm4StaticRecomp.Launcher.Core.Content;

namespace Fm4StaticRecomp.Launcher.Tests.Content;

public class UnicornCarsTests
{
    // unicorns.slt was written by SQLite (Python's sqlite3, page size 1024) with Data_Car's shape: 130
    // columns, the table under an interior page; three rows (395, 473, 1017) spill into overflow
    // pages, their flags among the spilled bytes.
    // Flags: 14 of the 17 cars at 1, Mugen Civic / Integra and Superleggera at 0; NUL_Car_00,
    // Warthog, Bentley 8 Litre, an F430 Scuderia name under another id and a mod's car at 1.
    private static readonly string Fixture = TestPath.Combine(AppContext.BaseDirectory, "Content", "unicorns.slt");
    private static readonly int[] Unlocked = [400, 401, 1004];

    private static string Game(TempDir temp)
    {
        var game = TestPath.Combine(temp.Path, "game");
        temp.File(TestPath.Combine("game", UnicornCars.Database), File.ReadAllBytes(Fixture));
        return game;
    }

    private static int[] Ids(IEnumerable<UnicornCar> cars) => cars.Select(c => c.Id).Order().ToArray();

    [Fact]
    public void Sorts_the_flagged_cars()
    {
        using var temp = new TempDir();
        var report = UnicornCars.Read(Game(temp));
        Assert.Equal(UnicornCars.Known.Select(c => c.Id).Except(Unlocked).Order(), Ids(report.Locked));
        Assert.Equal(Unlocked, Ids(report.Unlocked));
        Assert.Equal(["Warthog", "1931 Bentley 8 Litre", "FER_F430Scuderia_07", "MOD_Custom_20"],
            report.KeptLocked.OrderBy(c => c.Id).Select(c => c.Name));
    }

    [Fact]
    public void Unlocks_the_known_cars_and_nothing_else()
    {
        using var temp = new TempDir();
        var game = Game(temp);
        var db = TestPath.Combine(game, UnicornCars.Database);
        var before = File.ReadAllBytes(db);

        var done = UnicornCars.Unlock(game);

        Assert.Equal(14, done.Locked.Count);
        var after = UnicornCars.Read(game);
        Assert.Empty(after.Locked);
        Assert.Equal(UnicornCars.Known.Select(c => c.Id).Order(), Ids(after.Unlocked));
        Assert.Equal([1346, 1372, 2000, 2001], Ids(after.KeptLocked));
        // One serial type byte per car (constant 1 to constant 0) and the change counter, nothing else:
        // NUL_Car_00 and the other table keep their flags.
        var bytes = File.ReadAllBytes(db);
        var changed = Enumerable.Range(0, bytes.Length).Where(i => bytes[i] != before[i]).ToList();
        Assert.Equal(14, changed.Count(i => i >= 100));
        Assert.All(changed.Where(i => i >= 100), i => Assert.Equal((9, 8), (before[i], bytes[i])));
        Assert.All(changed.Where(i => i < 100), i => Assert.True(i is >= 24 and <= 27 or >= 92 and <= 95, $"header byte {i}"));
        Assert.Equal(BinaryPrimitives.ReadUInt32BigEndian(before.AsSpan(24)) + 1, BinaryPrimitives.ReadUInt32BigEndian(bytes.AsSpan(24)));
        Assert.False(File.Exists(db + ".new"));
    }

    [Fact]
    public void Keeps_the_first_original_as_the_backup()
    {
        using var temp = new TempDir();
        var game = Game(temp);
        var db = TestPath.Combine(game, UnicornCars.Database);
        var original = File.ReadAllBytes(db);
        UnicornCars.Unlock(game);
        Assert.Equal(original, File.ReadAllBytes(TestPath.Combine(game, UnicornCars.Backup)));

        File.WriteAllBytes(db, File.ReadAllBytes(Fixture));
        File.WriteAllBytes(TestPath.Combine(game, UnicornCars.Backup), "older"u8.ToArray());
        UnicornCars.Unlock(game);
        Assert.Equal("older", File.ReadAllText(TestPath.Combine(game, UnicornCars.Backup)));
    }

    [Fact]
    public void Leaves_an_unlocked_database_alone()
    {
        using var temp = new TempDir();
        var game = Game(temp);
        UnicornCars.Unlock(game);
        var db = TestPath.Combine(game, UnicornCars.Database);
        var unlocked = File.ReadAllBytes(db);
        var written = File.GetLastWriteTimeUtc(db);

        Assert.Empty(UnicornCars.Unlock(game).Locked);
        Assert.Equal(unlocked, File.ReadAllBytes(db));
        Assert.Equal(written, File.GetLastWriteTimeUtc(db));
    }

    [Fact]
    public void Never_writes_through_a_linked_game_file()
    {
        using var temp = new TempDir();
        var game = Game(temp);
        var db = TestPath.Combine(game, UnicornCars.Database);
        var other = TestPath.Combine(temp.Path, "other-install-gamedb.slt");
        File.Move(db, other);
        TestPath.CreateHardLink(db, other);
        UnicornCars.Unlock(game);
        Assert.Empty(UnicornCars.Read(game).Locked);
        Assert.Equal(File.ReadAllBytes(Fixture), File.ReadAllBytes(other));
    }

    [Theory]
    [InlineData("not a database")]
    [InlineData("SQLite format 3\0")]
    public void Refuses_a_file_it_cannot_read(string content)
    {
        using var temp = new TempDir();
        var game = TestPath.Combine(temp.Path, "game");
        temp.File(TestPath.Combine("game", UnicornCars.Database), System.Text.Encoding.ASCII.GetBytes(content));
        Assert.Throws<InvalidDataException>(() => UnicornCars.Read(game));
    }

    // Page 1 as a leaf with one cell whose record header claims more bytes than its payload has.
    [Fact]
    public void Refuses_a_damaged_record()
    {
        using var temp = new TempDir();
        var game = TestPath.Combine(temp.Path, "game");
        var bytes = File.ReadAllBytes(Fixture)[..1024];
        bytes[100] = 0x0D;
        BinaryPrimitives.WriteUInt16BigEndian(bytes.AsSpan(103), 1);
        BinaryPrimitives.WriteUInt16BigEndian(bytes.AsSpan(108), 1000);
        bytes[1000..1004].AsSpan().Clear();
        new byte[] { 0x02, 0x01, 0x7F, 0x00 }.CopyTo(bytes, 1000);
        temp.File(TestPath.Combine("game", UnicornCars.Database), bytes);
        Assert.Throws<InvalidDataException>(() => UnicornCars.Read(game));
    }

}
