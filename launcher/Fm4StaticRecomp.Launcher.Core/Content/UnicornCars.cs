// Copyright (c) 2026, i2kiselev
// BSD 3-Clause License: see LICENSE in the repository root.
using System.Buffers.Binary;

namespace Fm4StaticRecomp.Launcher.Core.Content;

public sealed record UnicornCar(int Id, string MediaName, string Name);

// Locked: known cars still flagged; Unlocked: known cars already buyable; KeptLocked: other flagged
// cars, left alone.
public sealed record UnicornReport(IReadOnlyList<UnicornCar> Locked, IReadOnlyList<UnicornCar> Unlocked, IReadOnlyList<UnicornCar> KeptLocked);

// Unicorn cars are the game's, not the saves': Data_Car.IsUnicorn in the game's database keeps a
// car out of the Autoshow for every profile. Unlocking clears that flag for the 17 unicorns of the
// 1.0 disc, all of them cars on Disc 1 or Disc 2. Rev 1 and Project Forza Plus 4 ship those already
// cleared and flag two others instead, which have only an Autovista model and stay locked.
public static class UnicornCars
{
    public const string Database = @"Media\db\gamedb.slt";
    public const string Backup = @"Media\db\gamedb.before-unicorns.slt";
    // SQLite's serial types for the integer constants 1 and 0.
    private const byte One = 9, Zero = 8;
    // The game's own placeholder row, flagged in every release: never a car.
    private const string Placeholder = "NUL_Car_00";

    public static readonly IReadOnlyList<UnicornCar> Known =
    [
        new(289, "CHE_CamaroSS_69", "1969 Chevrolet Camaro Super Sport"),
        new(297, "FER_360Challenge_03", "2003 Ferrari Challenge Stradale"),
        new(361, "MIT_HKS_06", "2006 Mitsubishi HKS CT230R"),
        new(394, "NIS_SkylineMinesR32_93", "1993 Nissan Mine's Skyline GT-R R32"),
        new(395, "NIS_SkylineMinesR34_02", "2002 Nissan Mine's Skyline GT-R R34"),
        new(400, "HON_CivicMugen_04", "2004 Honda Mugen Civic Type R"),
        new(401, "HON_IntegraMugen_02", "2002 Honda Mugen Integra Type R"),
        new(425, "MAZ_RX7SpiritR_02", "2002 Mazda RX-7 Spirit R"),
        new(473, "NIS_TopSecretSilvia_00", "2000 Nissan Top Secret Silvia"),
        new(475, "TOY_TopSecretSupra_98", "1998 Toyota Top Secret Supra"),
        new(565, "BMW_M3GTR_05", "2005 BMW M3 GTR"),
        new(569, "HON_NSXRGT_05", "2005 Honda NSX-R GT"),
        new(586, "SUB_S204_06", "2006 Subaru Impreza S204"),
        new(617, "SHE_GT500_07", "2007 Shelby GT500"),
        new(1004, "LAM_Superleggera_07", "2007 Lamborghini Gallardo Superleggera"),
        new(1017, "LAM_MurcielagoLP640_07", "2007 Lamborghini Murciélago LP640"),
        new(1022, "FER_F430Scuderia_07", "2007 Ferrari F430 Scuderia"),
    ];

    private static readonly Dictionary<string, string> AutovistaOnly = new(StringComparer.OrdinalIgnoreCase)
    {
        ["BUN_Warthog_10"] = "Warthog",
        ["BEN_8Litre_31"] = "1931 Bentley 8 Litre",
    };

    public static bool HasOnlyAutovistaModel(UnicornCar car) => AutovistaOnly.ContainsKey(car.MediaName);

    private sealed record Car(int Id, string MediaName, bool Flagged, long FlagOffset);

    public static UnicornReport Read(string gameFolder) => Sort(Load(gameFolder).Cars);

    // Returns what it found: Locked are the cars it unlocked. The first original is kept as Backup,
    // and the edit is written beside the database and renamed over it, so a game file hard-linked
    // from elsewhere is replaced, never written through.
    public static UnicornReport Unlock(string gameFolder)
    {
        var (path, data, cars) = Load(gameFolder);
        var report = Sort(cars);
        if (report.Locked.Count == 0) return report;
        foreach (var car in cars.Where(c => c.Flagged && report.Locked.Any(k => k.Id == c.Id)))
        {
            if (data[car.FlagOffset] != One)
                throw new InvalidDataException("The game's database stores a unicorn flag in a way the launcher does not know.");
            data[car.FlagOffset] = Zero;
        }
        var counter = BinaryPrimitives.ReadUInt32BigEndian(data.AsSpan(24));
        BinaryPrimitives.WriteUInt32BigEndian(data.AsSpan(24), counter + 1);
        if (BinaryPrimitives.ReadUInt32BigEndian(data.AsSpan(92)) == counter)
            BinaryPrimitives.WriteUInt32BigEndian(data.AsSpan(92), counter + 1);

        var backup = Path.Combine(gameFolder, Backup.Replace('\\', Path.DirectorySeparatorChar));
        if (!File.Exists(backup)) Replace(backup, File.ReadAllBytes(path));
        Replace(path, data);
        return report;
    }

    // Written beside and renamed into place: a copy cut short never stands as the file.
    private static void Replace(string path, byte[] data)
    {
        try
        {
            File.WriteAllBytes(path + ".new", data);
            File.Move(path + ".new", path, overwrite: true);
        }
        finally
        {
            if (File.Exists(path + ".new")) File.Delete(path + ".new");
        }
    }

    private static (string Path, byte[] Data, List<Car> Cars) Load(string gameFolder)
    {
        var path = Path.Combine(gameFolder, Database.Replace('\\', Path.DirectorySeparatorChar));
        var data = File.ReadAllBytes(path);
        try
        {
            var file = new SqliteFile(data);
            var (root, columns) = file.Table("Data_Car");
            int Column(string name)
            {
                for (var i = 0; i < columns.Count; i++)
                    if (columns[i].Equals(name, StringComparison.OrdinalIgnoreCase)) return i;
                throw new InvalidDataException($"The game's database has no Data_Car.{name}.");
            }
            int id = Column("Id"), media = Column("MediaName"), flag = Column("IsUnicorn");
            // An index holds its own copy of the flag, which a change in place would leave stale.
            if (file.Rows(1).Any(r => r.Value(0) is "index" && "Data_Car".Equals(r.Value(2) as string, StringComparison.OrdinalIgnoreCase) &&
                                      (r.Value(4) as string)?.Contains("IsUnicorn", StringComparison.OrdinalIgnoreCase) == true))
                throw new InvalidDataException("The game's database indexes the unicorn flag; the launcher cannot change it.");
            // Id is INTEGER PRIMARY KEY, stored as the rowid and null in the record.
            var cars = file.Rows(root).Select(r => new Car(
                (int)(r.Value(id) as long? ?? r.RowId),
                r.Value(media) as string ?? "",
                r.Value(flag) is long value && value != 0,
                r.Value(flag) is null ? -1 : r.TypeOffset(flag))).ToList();
            return (path, data, cars);
        }
        // SqliteFile checks what it relies on; a page damaged in other ways shows up as these.
        catch (Exception error) when (error is ArgumentException or IndexOutOfRangeException)
        {
            throw new InvalidDataException("The game's database is damaged.", error);
        }
    }

    private static UnicornReport Sort(List<Car> cars)
    {
        List<UnicornCar> locked = [], unlocked = [], kept = [];
        foreach (var car in cars)
        {
            var known = Known.FirstOrDefault(k => k.Id == car.Id && k.MediaName.Equals(car.MediaName, StringComparison.OrdinalIgnoreCase));
            if (known is not null) (car.Flagged ? locked : unlocked).Add(known);
            else if (car.Flagged && !car.MediaName.Equals(Placeholder, StringComparison.OrdinalIgnoreCase))
                kept.Add(new UnicornCar(car.Id, car.MediaName, AutovistaOnly.GetValueOrDefault(car.MediaName, car.MediaName)));
        }
        return new UnicornReport(locked, unlocked, kept);
    }
}
