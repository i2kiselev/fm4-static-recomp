// Copyright (c) 2026, i2kiselev
// BSD 3-Clause License: see LICENSE in the repository root.
using System.Text;
using System.Text.Json;
using Fm4StaticRecomp.Launcher.Core.Pfp4;
using Fm4StaticRecomp.Launcher.Tests.Discs;

namespace Fm4StaticRecomp.Launcher.Tests.Pfp4;

// A miniature Project Forza Plus 4: two packages (one with a patched file), four modes, and a
// matching manifest.
public sealed class FakeMod
{
    public const string PackageA = "AAAA0000000000000000000000000000000000004D";
    public const string PackageB = "BBBB0000000000000000000000000000000000004D";
    public const string Merge = @"Media\DLCZips\1_pri_65\Media\db\patch\1000_merge.slt";
    public const string Track = @"Media\Tracks\TrackSettings.xml";
    public const string Tunables = @"Media\GameTunableSettings\GameTunableSettings.ini";
    public static readonly string[] Modes = ["normal", "hard", "sandbox", "vanilla"];

    private readonly List<object> _gameFiles = [];
    private readonly Dictionary<string, object> _modes = [];
    private readonly List<object> _packages = [];

    public string Root { get; }
    public string ManifestPath { get; }

    public FakeMod(TempDir temp)
    {
        Root = Path.Combine(temp.Path, "mod", "Project Forza Plus 4");
        GameFile("default.xex", "mod xex");
        GameFile(Tunables, "main tunables");
        File("Main Game Changes\\Media\\RenderScenarios\\1-Car_Race.xml", "xenia scene");
        foreach (var mode in Modes)
        {
            var folder = $"Game Mode DBs\\{char.ToUpperInvariant(mode[0])}{mode[1..]} Mode";
            _modes[mode] = new
            {
                game_db = Entry($"{folder}\\gamedb.slt", @"Media\db\gamedb.slt", $"db {mode}"),
                harder_ai = new
                {
                    game_db = Entry($"{folder}\\Harder AI\\gamedb.slt", @"Media\db\gamedb.slt", $"harder db {mode}"),
                    tunables = Entry($"{folder}\\Harder AI\\GameTunableSettings.ini", Tunables, $"harder tunables {mode}"),
                },
                dlc_dbs = new[] { Entry($"{folder}\\DLC DBs\\{PackageA}\\{Merge}", $"{PackageA}\\{Merge}", $"merge {mode}") },
                skip_intro = new[]
                {
                    new
                    {
                        name = "Car",
                        files = new[] { Entry($"{folder}\\Skip Intro Saves\\Skip Intro Car\\ForzaProfile\\ForzaProfile", @"ForzaProfile\ForzaProfile", $"save {mode}") },
                    },
                },
            };
        }
        Package(PackageA, 1, "Car Pack", null, [(@"Media\1.puboffer", "offer"), (Merge, "merge mod")]);
        Package(PackageB, 21, "Track Pack", 0xFFFFFFFF, [(@"Media\21.puboffer", "offer"), (Track, "<Blur value=\"9999\"/>\r\n")]);
        ManifestPath = Path.Combine(temp.Path, "pfp4.json");
        System.IO.File.WriteAllBytes(ManifestPath, JsonSerializer.SerializeToUtf8Bytes(new
        {
            schema_version = 1,
            mod_version = "1.3",
            game_files = _gameFiles,
            modes = _modes,
            packages = _packages,
            patches = new[]
            {
                new
                {
                    target = $"{PackageB}\\{Track}",
                    replace = new[] { new { from = "<Blur value=\"9999\"/>", to = "<Blur value=\"60\"/>" } },
                    size = PatchedTrack.Length,
                    sha256 = TestDiscs.Sha(Encoding.UTF8.GetBytes(PatchedTrack)),
                },
            },
        }));
    }

    public const string PatchedTrack = "<Blur value=\"60\"/>\r\n";

    public Pfp4Manifest Manifest => Pfp4Manifest.Load(ManifestPath);

    public string ModFile(string relative) => Path.Combine(Root, relative.Replace('\\', Path.DirectorySeparatorChar));

    private void File(string relative, string text) =>
        System.IO.File.WriteAllText(CreateParent(Path.Combine(Root, relative.Replace('\\', Path.DirectorySeparatorChar))), text);

    private static string CreateParent(string path)
    {
        Directory.CreateDirectory(Path.GetDirectoryName(path)!);
        return path;
    }

    private object Entry(string source, string target, string text)
    {
        File(source, text);
        var data = Encoding.UTF8.GetBytes(text);
        return new { source, target, size = data.LongLength, sha256 = TestDiscs.Sha(data) };
    }

    private void GameFile(string target, string text) => _gameFiles.Add(Entry($"Main Game Changes\\{target}", target, text));

    private void Package(string folder, int offer, string name, uint? mask, (string Path, string Text)[] files) =>
        _packages.Add(new
        {
            folder,
            offer,
            display_name = name,
            license_mask = mask,
            files = files.Select(f => Entry($"Disc 2 and DLCs\\00000002\\{folder}\\{f.Path}", f.Path, f.Text)).ToArray(),
        });
}
