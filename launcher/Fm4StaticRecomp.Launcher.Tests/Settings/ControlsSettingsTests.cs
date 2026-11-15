// Copyright (c) 2026, i2kiselev
// BSD 3-Clause License: see LICENSE in the repository root.
using System.Text;
using Fm4StaticRecomp.Launcher.Core.Settings;

namespace Fm4StaticRecomp.Launcher.Tests.Settings;

public class ControlsSettingsTests
{
    private const ControlSet Driving = ControlSet.Driving, Menu = ControlSet.Menu;

    private static string Write(TempDir temp, string text) => temp.File("controls.toml", Encoding.UTF8.GetBytes(text));

    [Fact]
    public void Shows_defaults_without_a_file()
    {
        using var temp = new TempDir();
        var settings = new ControlsSettings(temp.File("controls.toml"));
        Assert.Equal(["Return", "Space"], settings.Keys(Menu, "a"));
        Assert.Equal(0.15, settings.Ramp("ramp_look_seconds"));
        Assert.False(settings.Changed);
    }

    [Fact]
    public void Reads_stored_values_and_drops_unknown_tokens()
    {
        using var temp = new TempDir();
        var settings = new ControlsSettings(Write(temp, "driving_x = \"Q,Foo,Numpad1\"\nmenu_a = \"\"\nmenu_b = \"Nope\"\n"));
        Assert.Equal(["Q", "Numpad1"], settings.Keys(Driving, "x"));
        Assert.Equal(["Return", "Space"], settings.Keys(Menu, "a"));
        Assert.Equal(["Escape", "Backspace"], settings.Keys(Menu, "b"));
    }

    [Fact]
    public void Keeps_a_combination_it_did_not_create()
    {
        using var temp = new TempDir();
        var path = Write(temp, "driving_x = \"Shift+Q\"\n");
        var settings = new ControlsSettings(path);
        Assert.Equal(["Shift+Q"], settings.Keys(Driving, "x"));
        settings.Reset(Driving, "x");
        Assert.Equal(["Q"], settings.Keys(Driving, "x"));
        settings.Save();
        Assert.DoesNotContain("driving_x", File.ReadAllText(path));
    }

    [Fact]
    public void Edit_then_save_writes_only_that_row()
    {
        using var temp = new TempDir();
        var path = Write(temp, "# my keys\r\nunknown = 1\r\ndriving_y = \"R\"\r\n");
        var settings = new ControlsSettings(path);
        settings.SetKey(Driving, "x", 0, "Z");
        Assert.True(settings.Changed);
        settings.Save();
        Assert.Equal("# my keys\nunknown = 1\ndriving_y = \"R\"\ndriving_x = \"Z\"\n", File.ReadAllText(path));
        Assert.False(settings.Changed);
    }

    [Fact]
    public void Saving_the_default_removes_the_line()
    {
        using var temp = new TempDir();
        var path = Write(temp, "driving_x = \"Z\"\nunknown = 1\n");
        var settings = new ControlsSettings(path);
        settings.SetKey(Driving, "x", 0, "Q");
        settings.Save();
        Assert.Equal("unknown = 1\n", File.ReadAllText(path));
    }

    [Fact]
    public void Nothing_changed_writes_no_file()
    {
        using var temp = new TempDir();
        var path = temp.File("controls.toml");
        var settings = new ControlsSettings(path);
        settings.Save();
        Assert.False(File.Exists(path));
        settings.SetKey(Driving, "x", 0, "Z");
        settings.SetKey(Driving, "x", 0, "Q");
        Assert.False(settings.Changed);
        settings.Save();
        Assert.False(File.Exists(path));
    }

    [Fact]
    public void Save_applies_on_top_of_what_the_game_wrote_meanwhile()
    {
        using var temp = new TempDir();
        var path = Write(temp, "unknown = 1\n");
        var settings = new ControlsSettings(path);
        File.WriteAllText(path, "unknown = 1\nramp_look_seconds = 0.3\n");
        settings.SetKey(Driving, "x", 0, "Z");
        settings.Save();
        var text = File.ReadAllText(path);
        Assert.Contains("ramp_look_seconds = 0.3", text);
        Assert.Contains("driving_x = \"Z\"", text);
    }

    [Fact]
    public void Add_and_remove_keys()
    {
        using var temp = new TempDir();
        var settings = new ControlsSettings(temp.File("controls.toml"));
        settings.AddKey(Driving, "a", "Z");
        Assert.Equal(["Space", "Z"], settings.Keys(Driving, "a"));
        settings.RemoveKey(Driving, "a", 1);
        Assert.Equal(["Space"], settings.Keys(Driving, "a"));
        settings.RemoveKey(Menu, "a", 0);
        Assert.Equal(["Space"], settings.Keys(Menu, "a"));
        Assert.Throws<InvalidOperationException>(() => settings.RemoveKey(Driving, "a", 0));
        Assert.Throws<ArgumentException>(() => settings.SetKey(Driving, "a", 0, "Shift+W"));
        Assert.Throws<ArgumentException>(() => settings.AddKey(Driving, "a", "Nope"));
    }

    [Fact]
    public void A_row_has_a_primary_and_one_optional_secondary_key()
    {
        using var temp = new TempDir();
        var settings = new ControlsSettings(temp.File("controls.toml"));
        Assert.Throws<InvalidOperationException>(() => settings.AddKey(Menu, "a", "Z"));
        settings.AddKey(Driving, "a", "Z");
        Assert.Throws<InvalidOperationException>(() => settings.AddKey(Driving, "a", "X"));
        Assert.Equal(["Space", "Z"], settings.Keys(Driving, "a"));
    }

    [Fact]
    public void Adding_a_key_the_row_already_has_is_a_no_op_even_when_full()
    {
        using var temp = new TempDir();
        var settings = new ControlsSettings(temp.File("controls.toml"));
        settings.AddKey(Menu, "a", "Space");
        Assert.Equal(["Return", "Space"], settings.Keys(Menu, "a"));
        Assert.False(settings.Changed);
    }

    [Fact]
    public void A_file_with_more_than_two_keys_is_shown_whole_and_kept()
    {
        using var temp = new TempDir();
        var path = Write(temp, "driving_x = \"Q,Z,X\"\n");
        var settings = new ControlsSettings(path);
        Assert.Equal(["Q", "Z", "X"], settings.Keys(Driving, "x"));
        settings.SetKey(Driving, "x", 1, "C");
        settings.Save();
        Assert.Contains("driving_x = \"Q,C,X\"", File.ReadAllText(path));
    }

    [Fact]
    public void Giving_a_slot_the_key_of_another_slot_swaps_them()
    {
        using var temp = new TempDir();
        var settings = new ControlsSettings(Write(temp, "driving_right_trigger = \"W,Up\"\ndriving_x = \"Q,Z,X\"\n"));
        settings.SetKey(Driving, "right_trigger", 0, "Up");
        Assert.Equal(["Up", "W"], settings.Keys(Driving, "right_trigger"));
        settings.SetKey(Driving, "right_trigger", 0, "Up");
        Assert.Equal(["Up", "W"], settings.Keys(Driving, "right_trigger"));
        settings.SetKey(Driving, "x", 0, "X");
        Assert.Equal(["X", "Z", "Q"], settings.Keys(Driving, "x"));
    }

    [Fact]
    public void The_defaults_do_not_conflict_with_each_other()
    {
        using var temp = new TempDir();
        var settings = new ControlsSettings(temp.File("controls.toml"));
        foreach (var set in Enum.GetValues<ControlSet>())
            foreach (var row in ControlsCatalog.Rows(set))
                Assert.True(settings.ConflictsWith(set, row.Id).Count == 0, $"{set} {row.Label}: {string.Join(", ", settings.ConflictsWith(set, row.Id))}");
    }

    [Fact]
    public void Conflicts_name_the_other_rows()
    {
        using var temp = new TempDir();
        var settings = new ControlsSettings(Write(temp, "driving_x = \"S\"\ndriving_left_trigger = \"S\"\n"));
        Assert.Equal(["Brake"], settings.ConflictsWith(Driving, "x"));
        Assert.Equal(["Gear down"], settings.ConflictsWith(Driving, "left_trigger"));
        Assert.Empty(settings.ConflictsWith(Menu, "left_trigger"));
        settings.Reset(Driving, "x");
        Assert.Empty(settings.ConflictsWith(Driving, "left_trigger"));
    }

    [Fact]
    public void A_key_you_bind_leaves_the_default_action_that_had_it()
    {
        using var temp = new TempDir();
        var settings = new ControlsSettings(Write(temp, "driving_x = \"S\"\n"));
        Assert.Empty(settings.ConflictsWith(Driving, "x"));
        Assert.Empty(settings.ConflictsWith(Driving, "left_trigger"));
        Assert.Equal(["Gear down"], settings.TakenFrom(Driving, "left_trigger").By);
        Assert.Equal(["S"], settings.TakenFrom(Driving, "left_trigger").Lost);
        Assert.Empty(settings.TakenFrom(Driving, "left_trigger").Kept);
        Assert.Empty(settings.TakenFrom(Driving, "x").By);
    }

    [Fact]
    public void An_edit_made_in_the_panel_counts_as_yours_before_it_is_saved()
    {
        using var temp = new TempDir();
        var settings = new ControlsSettings(temp.File("controls.toml"));
        Assert.Empty(settings.TakenFrom(Driving, "left_trigger").By);
        settings.SetKey(Driving, "x", 0, "S");
        Assert.Equal(["Gear down"], settings.TakenFrom(Driving, "left_trigger").By);
        Assert.Empty(settings.ConflictsWith(Driving, "left_trigger"));
        settings.Reset(Driving, "x");
        Assert.Empty(settings.TakenFrom(Driving, "left_trigger").By);
        Assert.False(settings.IsExplicit(Driving, "x"));
    }

    [Fact]
    public void A_line_equal_to_the_default_is_still_yours_as_the_game_reads_it()
    {
        using var temp = new TempDir();
        var settings = new ControlsSettings(Write(temp, "driving_x = \"Q\"\n"));
        Assert.True(settings.IsExplicit(Driving, "x"));
        Assert.False(settings.IsExplicit(Driving, "y"));
    }

    [Fact]
    public void A_partly_taken_default_is_named_too()
    {
        using var temp = new TempDir();
        var settings = new ControlsSettings(Write(temp, "menu_x = \"W\"\n"));
        var taken = settings.TakenFrom(Menu, "dpad_up");
        Assert.Equal(["X button"], taken.By);
        Assert.Equal(["W"], taken.Lost);
        Assert.Equal(["Up"], taken.Kept);
        Assert.Empty(settings.ConflictsWith(Menu, "dpad_up"));
    }

    [Fact]
    public void A_combination_conflicts_with_its_modifier_key_and_takes_its_bare_key_from_the_default()
    {
        using var temp = new TempDir();
        var settings = new ControlsSettings(Write(temp, "driving_x = \"Shift+W\"\ndriving_y = \"Ctrl+Q\"\n"));
        Assert.Equal(["Clutch"], settings.ConflictsWith(Driving, "x"));
        Assert.Equal(["Gear down"], settings.ConflictsWith(Driving, "left_shoulder"));
        Assert.Equal(["Gear down"], settings.TakenFrom(Driving, "right_trigger").By);
        Assert.DoesNotContain("Rewind", settings.ConflictsWith(Driving, "x"));
    }

    [Fact]
    public void Names_the_game_treats_as_one_key_conflict()
    {
        using var temp = new TempDir();
        var settings = new ControlsSettings(Write(temp, "driving_x = \"Ctrl+Q\"\ndriving_y = \"Control+Q\"\nmenu_x = \"NumpadEnter\"\n"));
        Assert.Contains("Rewind", settings.ConflictsWith(Driving, "x"));
        Assert.Contains("Select", settings.ConflictsWith(Menu, "x"));
    }

    [Fact]
    public void Mouse_buttons_written_in_the_file_are_shown_and_kept()
    {
        using var temp = new TempDir();
        var path = Write(temp, "driving_right_trigger = \"W,LMB\"\n");
        var settings = new ControlsSettings(path);
        Assert.Equal(["W", "LMB"], settings.Keys(Driving, "right_trigger"));
        settings.SetKey(Driving, "right_trigger", 0, "Up");
        settings.Save();
        Assert.Contains("driving_right_trigger = \"Up,LMB\"", File.ReadAllText(path));
    }

    [Fact]
    public void A_failed_save_leaves_the_saved_values_as_they_were()
    {
        using var temp = new TempDir();
        var path = Path.Combine(temp.Path, "controls.toml");
        Directory.CreateDirectory(path);
        var settings = new ControlsSettings(path);
        settings.SetKey(Driving, "x", 0, "Z");
        Assert.ThrowsAny<Exception>(settings.Save);
        settings.SetKey(Driving, "x", 0, "W");
        settings.SetKey(Driving, "x", 0, "Z");
        Assert.True(settings.Changed);
    }

    [Fact]
    public void Two_equal_keys_in_one_row_conflict_only_with_other_rows()
    {
        using var temp = new TempDir();
        var settings = new ControlsSettings(Write(temp, "driving_x = \"W,W\"\ndriving_right_trigger = \"W\"\n"));
        Assert.Equal(["Throttle"], settings.ConflictsWith(Driving, "x"));
    }

    [Fact]
    public void Resetting_a_row_onto_a_key_another_row_holds_hands_it_to_that_row()
    {
        using var temp = new TempDir();
        var settings = new ControlsSettings(Write(temp, "driving_b = \"Z\"\ndriving_left_trigger = \"E\"\n"));
        Assert.Empty(settings.ConflictsWith(Driving, "b"));
        settings.Reset(Driving, "b");
        Assert.Empty(settings.ConflictsWith(Driving, "b"));
        Assert.Equal(["Brake"], settings.TakenFrom(Driving, "b").By);
    }

    [Fact]
    public void Reset_all_restores_the_open_set_only()
    {
        using var temp = new TempDir();
        var settings = new ControlsSettings(temp.File("controls.toml"));
        settings.SetKey(Driving, "x", 0, "Z");
        settings.SetKey(Menu, "x", 0, "Z");
        settings.ResetAll(Driving);
        Assert.Equal(["Q"], settings.Keys(Driving, "x"));
        Assert.Equal(["Z"], settings.Keys(Menu, "x"));
        Assert.True(settings.IsDefault(Driving, "x"));
        Assert.False(settings.IsDefault(Menu, "x"));
    }

    [Fact]
    public void Ramp_is_clamped_saved_and_removed_at_default()
    {
        using var temp = new TempDir();
        var path = temp.File("controls.toml");
        var settings = new ControlsSettings(path);
        const string key = "ramp_look_seconds";
        settings.SetRamp(key, 1.0);
        Assert.Equal(0.60, settings.Ramp(key));
        settings.SetRamp(key, 0.2);
        settings.Save();
        Assert.Equal("ramp_look_seconds = 0.2\n", File.ReadAllText(path));
        Assert.False(settings.RampIsDefault(key));
        settings.ResetRamp(key);
        Assert.True(settings.RampIsDefault(key));
        settings.Save();
        Assert.Equal("", File.ReadAllText(path));
    }

    [Fact]
    public void Mouse_look_is_saved_bare_and_removed_at_default()
    {
        using var temp = new TempDir();
        var path = temp.File("controls.toml");
        var settings = new ControlsSettings(path);
        Assert.True(settings.MouseLook);
        Assert.Equal(25, settings.MouseLookSensitivity);
        Assert.True(settings.MouseLookIsDefault);
        settings.MouseLook = false;
        settings.SetMouseLookSensitivity(40);
        Assert.Equal(40, settings.MouseLookSensitivity);
        settings.SetKey(Driving, "x", 0, "Z");
        settings.Save();
        var text = File.ReadAllText(path);
        Assert.Contains("mouse_look = false\n", text);
        Assert.Contains("mouse_look_sensitivity = 40\n", text);
        Assert.Contains("driving_x = \"Z\"\n", text);
        Assert.False(settings.MouseLookIsDefault);
        settings.ResetMouseLook();
        Assert.True(settings.MouseLookIsDefault);
        settings.Save();
        Assert.Equal("driving_x = \"Z\"\n", File.ReadAllText(path));
    }

    [Fact]
    public void Mouse_look_sensitivity_is_clamped()
    {
        using var temp = new TempDir();
        var settings = new ControlsSettings(temp.File("controls.toml"));
        settings.SetMouseLookSensitivity(500);
        Assert.Equal(100, settings.MouseLookSensitivity);
        settings.SetMouseLookSensitivity(0);
        Assert.Equal(10, settings.MouseLookSensitivity);
    }

    [Theory]
    [InlineData("mouse_look = yes\n")]
    [InlineData("mouse_look = 1\n")]
    [InlineData("mouse_look_sensitivity = 0\n")]
    [InlineData("mouse_look_sensitivity = -1\n")]
    [InlineData("mouse_look_sensitivity = fast\n")]
    [InlineData("mouse_look_sensitivity = inf\n")]
    [InlineData("mouse_look_sensitivity = Infinity\n")]
    [InlineData("mouse_look_sensitivity = NaN\n")]
    public void Unreadable_mouse_look_values_read_as_the_defaults(string text)
    {
        using var temp = new TempDir();
        var settings = new ControlsSettings(Write(temp, text));
        Assert.True(settings.MouseLook);
        Assert.Equal(25, settings.MouseLookSensitivity);
    }

    [Theory]
    [InlineData("500", 100)]
    [InlineData("1e300", 100)]
    [InlineData("3", 10)]
    [InlineData("40.6", 41)]
    [InlineData("40.5", 41)]
    public void A_saved_sensitivity_is_rounded_and_held_to_10_to_100_as_the_game_does(string stored, int expected)
    {
        using var temp = new TempDir();
        var settings = new ControlsSettings(Write(temp, $"mouse_look_sensitivity = {stored}\n"));
        Assert.Equal(expected, settings.MouseLookSensitivity);
    }

    [Fact]
    public void Reads_saved_mouse_look_values()
    {
        using var temp = new TempDir();
        var settings = new ControlsSettings(Write(temp, "mouse_look = false\nmouse_look_sensitivity = 60\n"));
        Assert.False(settings.MouseLook);
        Assert.Equal(60, settings.MouseLookSensitivity);
    }

    [Fact]
    public void Reports_a_file_that_is_not_key_value_lines()
    {
        using var temp = new TempDir();
        Assert.True(new ControlsSettings(Write(temp, "[section]\nnot a pair\n")).FileIsMalformed);
        Assert.False(new ControlsSettings(Write(temp, "# fine\na = 1\n")).FileIsMalformed);
    }

    [Theory]
    [InlineData("0.1.3", false)]
    [InlineData("0.2.2", false)]
    [InlineData("0.3.1", false)]
    [InlineData("0.3.2", true)]
    [InlineData("0.4.0", true)]
    [InlineData("1.0.0", true)]
    [InlineData("weird", true)]
    public void Supports_by_version(string version, bool expected) => Assert.Equal(expected, ControlsSettings.Supports(version));
}
