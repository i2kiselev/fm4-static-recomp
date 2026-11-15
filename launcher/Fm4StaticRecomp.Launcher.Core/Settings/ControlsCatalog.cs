// Copyright (c) 2026, i2kiselev
// BSD 3-Clause License: see LICENSE in the repository root.
namespace Fm4StaticRecomp.Launcher.Core.Settings;

public enum ControlSet { Driving, Menu }

// Id is the name after menu_ / driving_ in controls.toml; Pad is the button as printed on a pad;
// Other puts the row under OTHER BUTTONS.
public sealed record ControlRow(string Id, string Label, string Pad, bool Other);

public sealed record RampRow(string Key, string Label, double Default);

// The controls the game's keyboard driver offers and their defaults. The defaults are the game's
// (src\input\controls.cpp DefaultControls): a test compares them with that file.
public static class ControlsCatalog
{
    public static IReadOnlyList<ControlRow> Rows(ControlSet set) => set == ControlSet.Driving ? DrivingRows : MenuRows;

    public static string DefaultKeys(ControlSet set, string id) => (set == ControlSet.Driving ? DrivingDefaults : MenuDefaults)[id];

    public static string FileKey(ControlSet set, string id) => (set == ControlSet.Driving ? "driving_" : "menu_") + id;

    // Hold the right mouse button in a race to look around the car.
    public const string MouseLookKey = "mouse_look", MouseLookSensitivityKey = "mouse_look_sensitivity";
    public const bool MouseLookDefault = true;
    public const int MouseLookSensitivityDefault = 25;

    public static IReadOnlyList<RampRow> Ramps { get; } =
    [
        new("ramp_steer_seconds", "Steering: rest to full", 0.17),
        new("ramp_steer_return_seconds", "Steering: back to rest", 0.10),
        new("ramp_pedal_seconds", "Throttle and brake: rest to full", 0.10),
        new("ramp_pedal_return_seconds", "Throttle and brake: back to rest", 0.08),
        new("ramp_look_seconds", "Looking around", 0.15),
    ];

    private static readonly ControlRow[] DrivingRows =
    [
        new("right_trigger", "Throttle", "RT", false),
        new("left_trigger", "Brake", "LT", false),
        new("lstick_left", "Steer left", "L-STICK LEFT", false),
        new("lstick_right", "Steer right", "L-STICK RIGHT", false),
        new("a", "Handbrake", "A", false),
        new("b", "Gear up", "B", false),
        new("x", "Gear down", "X", false),
        new("left_shoulder", "Clutch", "LB", false),
        new("y", "Rewind", "Y", false),
        new("back", "Camera", "BACK", false),
        new("right_shoulder", "Look straight back", "RB", false),
        new("rstick_up", "Look up", "R-STICK UP", false),
        new("rstick_down", "Look down (and back)", "R-STICK DOWN", false),
        new("rstick_left", "Look left", "R-STICK LEFT", false),
        new("rstick_right", "Look right", "R-STICK RIGHT", false),
        new("rstick_press", "Heat and damage", "R-STICK PRESS", false),
        new("dpad_up", "Telemetry up", "D-PAD UP", false),
        new("dpad_down", "Telemetry down", "D-PAD DOWN", false),
        new("start", "Pause", "START", false),
        new("lstick_up", "Left stick up", "L-STICK UP", true),
        new("lstick_down", "Left stick down", "L-STICK DOWN", true),
        new("lstick_press", "Left stick press", "L-STICK PRESS", true),
        new("dpad_left", "D-pad left", "D-PAD LEFT", true),
        new("dpad_right", "D-pad right", "D-PAD RIGHT", true),
    ];

    private static readonly ControlRow[] MenuRows =
    [
        new("a", "Select", "A", false),
        new("b", "Back", "B", false),
        new("x", "X button", "X", false),
        new("y", "Y button", "Y", false),
        new("left_shoulder", "LB button", "LB", false),
        new("right_shoulder", "RB button", "RB", false),
        new("left_trigger", "LT button", "LT", false),
        new("right_trigger", "RT button", "RT", false),
        new("dpad_up", "Move up", "D-PAD UP", false),
        new("dpad_down", "Move down", "D-PAD DOWN", false),
        new("dpad_left", "Move left", "D-PAD LEFT", false),
        new("dpad_right", "Move right", "D-PAD RIGHT", false),
        new("back", "BACK button", "BACK", false),
        new("start", "START button", "START", false),
        new("lstick_up", "Left stick up", "L-STICK UP", true),
        new("lstick_down", "Left stick down", "L-STICK DOWN", true),
        new("lstick_left", "Left stick left", "L-STICK LEFT", true),
        new("lstick_right", "Left stick right", "L-STICK RIGHT", true),
        new("lstick_press", "Left stick press", "L-STICK PRESS", true),
        new("rstick_up", "Right stick up", "R-STICK UP", true),
        new("rstick_down", "Right stick down", "R-STICK DOWN", true),
        new("rstick_left", "Right stick left", "R-STICK LEFT", true),
        new("rstick_right", "Right stick right", "R-STICK RIGHT", true),
        new("rstick_press", "Right stick press", "R-STICK PRESS", true),
    ];

    private static readonly Dictionary<string, string> MenuDefaults = new()
    {
        ["a"] = "Return,Space", ["b"] = "Escape,Backspace", ["x"] = "X", ["y"] = "Y",
        ["left_shoulder"] = "Q", ["right_shoulder"] = "E", ["left_trigger"] = "Z", ["right_trigger"] = "C",
        ["lstick_up"] = "Numpad8", ["lstick_down"] = "Numpad2", ["lstick_left"] = "Numpad4", ["lstick_right"] = "Numpad6", ["lstick_press"] = "F",
        ["rstick_up"] = "I", ["rstick_down"] = "K", ["rstick_left"] = "J", ["rstick_right"] = "L", ["rstick_press"] = "G",
        ["dpad_up"] = "Up,W", ["dpad_down"] = "Down,S", ["dpad_left"] = "Left,A", ["dpad_right"] = "Right,D",
        ["back"] = "Tab", ["start"] = "P",
    };

    private static readonly Dictionary<string, string> DrivingDefaults = new()
    {
        ["a"] = "Space", ["b"] = "E", ["x"] = "Q", ["y"] = "R",
        ["left_shoulder"] = "Shift", ["right_shoulder"] = "C", ["left_trigger"] = "S", ["right_trigger"] = "W",
        ["lstick_up"] = "Numpad8", ["lstick_down"] = "Numpad2", ["lstick_left"] = "A", ["lstick_right"] = "D", ["lstick_press"] = "V",
        ["rstick_up"] = "Up", ["rstick_down"] = "Down", ["rstick_left"] = "Left", ["rstick_right"] = "Right", ["rstick_press"] = "H",
        ["dpad_up"] = "T", ["dpad_down"] = "G", ["dpad_left"] = "Comma", ["dpad_right"] = "Period",
        ["back"] = "Tab", ["start"] = "Escape",
    };
}
