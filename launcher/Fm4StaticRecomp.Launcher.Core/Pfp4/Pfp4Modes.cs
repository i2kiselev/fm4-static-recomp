// Copyright (c) 2026, i2kiselev
// BSD 3-Clause License: see LICENSE in the repository root.
using System.Security.Cryptography;
using Fm4StaticRecomp.Launcher.Core.Install;
using Fm4StaticRecomp.Launcher.Core.Setup;

namespace Fm4StaticRecomp.Launcher.Core.Pfp4;

// Changes a Project Forza Plus 4 install's career mode and Harder AI from the files
// Pfp4Installer cached for every mode, without the mod.
public static class Pfp4Modes
{
    private const string GameDb = @"Media\db\gamedb.slt";
    private const string Packages = @"4D530910\00000002";
    public const string ChooseTheModAgain = "CHOOSE THE MOD AGAIN in CONTENT puts them back.";

    public static void Switch(InstallLayout layout, Pfp4Manifest manifest, SetupState state, string mode, bool harderAi)
    {
        var chosen = manifest.Modes[mode];
        var cache = Path.Combine(layout.Pfp4Modes, mode);
        var copies = new List<(string Source, Pfp4Entry Entry, string Destination)>
        {
            harderAi
                ? (Path.Combine(cache, "harder-ai", "gamedb.slt"), chosen.HarderAi.GameDb, Under(layout.Game, GameDb))
                : (Path.Combine(cache, "gamedb.slt"), chosen.GameDb, Under(layout.Game, GameDb)),
            harderAi
                ? (Path.Combine(cache, "harder-ai", "GameTunableSettings.ini"), chosen.HarderAi.Tunables, Under(layout.Game, Pfp4Manifest.Tunables))
                : (Path.Combine(layout.Pfp4, "GameTunableSettings.ini"), manifest.MainTunables, Under(layout.Game, Pfp4Manifest.Tunables)),
        };
        copies.AddRange(chosen.DlcDbs.Select(db =>
            (Under(Path.Combine(cache, "dlc"), db.Target), db, Under(Under(layout.Content, Packages), db.Target))));

        // Every file is checked before the first one is copied: a damaged cache leaves the
        // current mode whole.
        foreach (var (source, entry, _) in copies)
            if (!Matches(source, entry))
                throw new InvalidDataException(
                    $"The saved files of the {mode} mode are missing or damaged ({source}). {ChooseTheModAgain}");

        // Copies beside the targets, renamed over them only once all are written: a copy cut
        // short leaves the current mode whole, and a game file hard-linked from elsewhere is
        // replaced, never written through.
        try
        {
            foreach (var (source, _, destination) in copies) File.Copy(source, destination + ".new", overwrite: true);
        }
        catch
        {
            foreach (var (_, _, destination) in copies)
                if (File.Exists(destination + ".new")) File.Delete(destination + ".new");
            throw;
        }
        foreach (var (_, _, destination) in copies) File.Move(destination + ".new", destination, overwrite: true);
        state.Pfp4Mode = mode;
        state.Pfp4HarderAi = harderAi;
        state.Save();
    }

    private static bool Matches(string path, Pfp4Entry entry)
    {
        if (!File.Exists(path) || new FileInfo(path).Length != entry.Size) return false;
        using var stream = File.OpenRead(path);
        return Convert.ToHexString(SHA256.HashData(stream)).Equals(entry.Sha256, StringComparison.OrdinalIgnoreCase);
    }

    private static string Under(string root, string relative) =>
        Path.Combine(root, relative.Replace('\\', Path.DirectorySeparatorChar));
}
