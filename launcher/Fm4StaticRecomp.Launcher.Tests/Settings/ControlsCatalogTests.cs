// Copyright (c) 2026, i2kiselev
// BSD 3-Clause License: see LICENSE in the repository root.
using System.Globalization;
using System.Text.RegularExpressions;
using Fm4StaticRecomp.Launcher.Core.Settings;

namespace Fm4StaticRecomp.Launcher.Tests.Settings;

public partial class ControlsCatalogTests
{
    private static readonly string[] Ids =
    [
        "a", "b", "x", "y", "left_shoulder", "right_shoulder", "left_trigger", "right_trigger",
        "lstick_up", "lstick_down", "lstick_left", "lstick_right", "lstick_press",
        "rstick_up", "rstick_down", "rstick_left", "rstick_right", "rstick_press",
        "dpad_up", "dpad_down", "dpad_left", "dpad_right", "back", "start",
    ];

    [Theory]
    [InlineData(ControlSet.Driving)]
    [InlineData(ControlSet.Menu)]
    public void Each_set_has_all_24_controls_once(ControlSet set)
    {
        var rows = ControlsCatalog.Rows(set);
        Assert.Equal(Ids.OrderBy(i => i), rows.Select(r => r.Id).OrderBy(i => i));
        Assert.All(rows, r => Assert.False(string.IsNullOrWhiteSpace(r.Label)));
        Assert.All(rows, r => Assert.False(string.IsNullOrWhiteSpace(r.Pad)));
        Assert.Contains(rows, r => !r.Other);
        Assert.Contains(rows, r => r.Other);
    }

    [Fact]
    public void Main_rows_come_before_other_buttons()
    {
        foreach (var set in Enum.GetValues<ControlSet>())
        {
            var rows = ControlsCatalog.Rows(set);
            Assert.Equal(rows.OrderBy(r => r.Other).Select(r => r.Id), rows.Select(r => r.Id));
        }
    }

    [Fact]
    public void Defaults_are_known_binds()
    {
        foreach (var set in Enum.GetValues<ControlSet>())
            foreach (var row in ControlsCatalog.Rows(set))
            {
                var keys = KeyNames.Split(ControlsCatalog.DefaultKeys(set, row.Id));
                Assert.NotEmpty(keys);
                Assert.All(keys, k => Assert.True(KeyNames.IsBind(k), $"{set} {row.Id}: {k}"));
            }
    }

    [Fact]
    public void File_keys_carry_the_set_prefix()
    {
        Assert.Equal("menu_left_shoulder", ControlsCatalog.FileKey(ControlSet.Menu, "left_shoulder"));
        Assert.Equal("driving_a", ControlsCatalog.FileKey(ControlSet.Driving, "a"));
    }

    [Fact]
    public void Has_the_five_ramps_in_the_games_order()
    {
        Assert.Equal(
            ["ramp_steer_seconds", "ramp_steer_return_seconds", "ramp_pedal_seconds", "ramp_pedal_return_seconds", "ramp_look_seconds"],
            ControlsCatalog.Ramps.Select(r => r.Key));
    }

    [Fact]
    public void Defaults_match_the_games_source()
    {
        var source = File.ReadAllText(FindControlsSource());
        var found = 0;
        foreach (Match match in KeyLine().Matches(source))
        {
            var set = match.Groups[1].Value == "menu" ? ControlSet.Menu : ControlSet.Driving;
            var id = IdOf(match.Groups[2].Value);
            Assert.Equal(match.Groups[3].Value, ControlsCatalog.DefaultKeys(set, id));
            found++;
        }
        Assert.Equal(48, found);

        var ramp = RampLine().Match(source);
        Assert.True(ramp.Success, "the controls.ramp line is missing in controls.cpp");
        var values = ramp.Groups[1].Value.Split(',').Select(v => double.Parse(v, CultureInfo.InvariantCulture)).ToArray();
        Assert.Equal(values, ControlsCatalog.Ramps.Select(r => r.Default));

        var look = MouseLookLine().Match(source);
        Assert.True(look.Success, "the controls.mouse_look line is missing in controls.cpp");
        Assert.Equal(ControlsCatalog.MouseLookDefault, look.Groups[1].Value == "true");
        Assert.Equal(ControlsCatalog.MouseLookSensitivityDefault, int.Parse(look.Groups[2].Value, CultureInfo.InvariantCulture));
    }

    // kLeftShoulder -> left_shoulder, kLStickUp -> lstick_up, kDpadUp -> dpad_up.
    private static string IdOf(string enumerator)
    {
        var name = enumerator.Replace("LStick", "Lstick_").Replace("RStick", "Rstick_");
        name = Regex.Replace(name, "(?<!^)(?<!_)([A-Z])", "_$1");
        return name.ToLowerInvariant();
    }

    private static string FindControlsSource()
    {
        var searched = new List<string>();
        for (var dir = new DirectoryInfo(AppContext.BaseDirectory); dir is not null; dir = dir.Parent)
        {
            var candidate = Path.Combine(dir.FullName, "src", "input", "controls.cpp");
            if (File.Exists(candidate)) return candidate;
            searched.Add(candidate);
        }
        throw new FileNotFoundException("src\\input\\controls.cpp not found above the test folder. Looked at: " + string.Join("; ", searched));
    }

    [GeneratedRegex(@"(menu|driving)\.keys\[size_t\(PadControl::k(\w+)\)\]\s*=\s*""([^""]+)"";")]
    private static partial Regex KeyLine();

    [GeneratedRegex(@"controls\.ramp\s*=\s*\{([^}]+)\};")]
    private static partial Regex RampLine();

    [GeneratedRegex(@"controls\.mouse_look\s*=\s*\{(true|false),\s*([0-9]+)\};")]
    private static partial Regex MouseLookLine();
}
