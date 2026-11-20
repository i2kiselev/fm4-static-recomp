// Copyright (c) 2026, i2kiselev
// BSD 3-Clause License: see LICENSE in the repository root.
using Fm4StaticRecomp.Launcher.Core.Settings;

namespace Fm4StaticRecomp.Launcher.Core.Wheel;

public enum WheelGuideStep { TurnLeft, Throttle, Brake, Clutch, Handbrake, ShiftUp, ShiftDown, Pause, MenuSelect, MenuBack }

// The first-time set-up: one input per step, written into the settings as it is accepted.
public sealed class WheelGuide(WheelSettings settings)
{
    private static readonly Dictionary<WheelGuideStep, string> Prompts = new()
    {
        [WheelGuideStep.TurnLeft] = "Turn the wheel all the way to the left.",
        [WheelGuideStep.Throttle] = "Press the throttle pedal all the way down.",
        [WheelGuideStep.Brake] = "Press the brake pedal all the way down.",
        [WheelGuideStep.Clutch] = "Press the clutch pedal all the way down, or press SKIP.",
        [WheelGuideStep.Handbrake] = "Pull the handbrake, or press SKIP.",
        [WheelGuideStep.ShiftUp] = "Pull the paddle or press the button you want for shifting up.",
        [WheelGuideStep.ShiftDown] = "Pull the paddle or press the button you want for shifting down.",
        [WheelGuideStep.Pause] = "Press the button you want for pausing the game.",
        [WheelGuideStep.MenuSelect] = "Press the button you want for selecting in menus.",
        [WheelGuideStep.MenuBack] = "Press the button you want for going back in menus.",
    };

    // Pad A selects in menus and is the clutch action's button; pad B goes back and is the handbrake's.
    private static readonly Dictionary<WheelGuideStep, string> ButtonActions = new()
    {
        [WheelGuideStep.ShiftUp] = "shift_up",
        [WheelGuideStep.ShiftDown] = "shift_down",
        [WheelGuideStep.Pause] = "start",
        [WheelGuideStep.MenuSelect] = "clutch",
        [WheelGuideStep.MenuBack] = "handbrake",
    };

    private readonly List<(uint, int)> _takenAxes = [];
    private readonly HashSet<string> _setActions = [];

    public WheelGuideStep? Current { get; private set; } = WheelGuideStep.TurnLeft;
    public bool Done => Current is null;
    public string? Prompt => Current is { } step ? Prompts[step] : null;
    public bool CanSkip => Current is WheelGuideStep.Clutch or WheelGuideStep.Handbrake;

    public WheelCapture NewCapture() => Current switch
    {
        WheelGuideStep.TurnLeft => WheelCapture.ForAxis(steering: true, _takenAxes),
        WheelGuideStep.Throttle or WheelGuideStep.Brake or WheelGuideStep.Clutch => WheelCapture.ForAxis(steering: false, _takenAxes),
        WheelGuideStep.Handbrake => WheelCapture.ForAxisOrButton(_takenAxes),
        null => throw new InvalidOperationException("The set-up is done."),
        _ => WheelCapture.ForButton(),
    };

    public void Accept(CaptureResult result)
    {
        var step = Current ?? throw new InvalidOperationException("The set-up is done.");
        if (result.Kind == CaptureKind.Axis) _takenAxes.Add((result.DeviceId, result.Index));
        switch (step)
        {
            case WheelGuideStep.TurnLeft:
                settings.SetAxis(WheelAxis.Steering, AxisBinding(result));
                // Left should read negative.
                settings.SteeringInvert = result.Positive;
                break;
            case WheelGuideStep.Throttle:
                settings.SetAxis(WheelAxis.Throttle, AxisBinding(result));
                break;
            case WheelGuideStep.Brake:
                settings.SetAxis(WheelAxis.Brake, AxisBinding(result));
                break;
            case WheelGuideStep.Clutch:
                settings.SetAxis(WheelAxis.Clutch, AxisBinding(result));
                break;
            case WheelGuideStep.Handbrake when result.Kind == CaptureKind.Axis:
                settings.SetAxis(WheelAxis.Handbrake, result.Binding);
                settings.SetButton("handbrake", null);
                break;
            case WheelGuideStep.Handbrake:
                settings.SetAxis(WheelAxis.Handbrake, null);
                SetButton("handbrake", result);
                break;
            default:
                SetButton(ButtonActions[step], result);
                break;
        }
        Advance();
    }

    public void Skip()
    {
        if (!CanSkip) throw new InvalidOperationException($"{Current} cannot be skipped.");
        settings.SetAxis(Current == WheelGuideStep.Clutch ? WheelAxis.Clutch : WheelAxis.Handbrake, null);
        if (Current == WheelGuideStep.Handbrake) settings.SetButton("handbrake", null);
        Advance();
    }

    private void SetButton(string action, CaptureResult result)
    {
        if (result.Kind == CaptureKind.Axis) throw new ArgumentException("A button step needs a button.", nameof(result));
        settings.SetButton(action, result.Binding);
        _setActions.Add(action);
    }

    private static string AxisBinding(CaptureResult result) =>
        result.Kind == CaptureKind.Axis ? result.Binding : throw new ArgumentException("This step needs an axis.", nameof(result));

    // Menu select and back are asked only when no earlier step of this run gave pad A or B a button.
    private void Advance()
    {
        var next = Current is { } step && step < WheelGuideStep.MenuBack ? step + 1 : (WheelGuideStep?)null;
        while (next is { } candidate && ButtonActions.TryGetValue(candidate, out var action)
               && candidate is WheelGuideStep.MenuSelect or WheelGuideStep.MenuBack && _setActions.Contains(action))
            next = candidate < WheelGuideStep.MenuBack ? candidate + 1 : null;
        Current = next;
    }
}
