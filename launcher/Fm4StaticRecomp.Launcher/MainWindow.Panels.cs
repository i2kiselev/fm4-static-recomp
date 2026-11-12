// Portions copyright (c) 2026, Pinyon Shift contributors (BSD 3-Clause: see THIRD_PARTY_NOTICES.md).
// Copyright (c) 2026, i2kiselev (BSD 3-Clause: see LICENSE in the repository root).
// Adapted from Pinyon Shift's launcher (launcher/PinyonShift.Launcher.Desktop, BSD-3-Clause; see THIRD_PARTY_NOTICES.md).
using Avalonia.Controls;
using Avalonia.Interactivity;
using Avalonia.Threading;

namespace Fm4StaticRecomp.Launcher;

// One panel at a time under Play; the open panel's button is marked and a second click
// closes it.
public partial class MainWindow
{
    private enum Panel { None, Content, Graphics, Settings, Controls, Wheel }

    private Panel _panel = Panel.None;

    private static bool IsSettings(Panel panel) => panel is Panel.Graphics or Panel.Settings;

    private void ShowPanel(Panel panel)
    {
        _panel = panel;
        ContentPanel.IsVisible = panel == Panel.Content;
        SettingsPanel.IsVisible = IsSettings(panel);
        ControlsPanel.IsVisible = panel == Panel.Controls;
        WheelPanel.IsVisible = panel == Panel.Wheel;
        if (panel != Panel.Controls) EndControlsCapture();
        if (panel != Panel.Wheel) StopWheelProbe();
        foreach (var (button, owner) in new (Button, Panel)[]
                     { (ContentButton, Panel.Content), (GraphicsButton, Panel.Graphics), (SettingsButton, Panel.Settings),
                       (ControlsButton, Panel.Controls), (WheelButton, Panel.Wheel) })
            button.Classes.Set("selected", owner == panel);
    }

    private void OpenPanel(Panel panel)
    {
        if (_mode != Mode.Ready) return;
        if (_panel == panel)
        {
            ShowPanel(Panel.None);
            return;
        }
        // GRAPHICS and SETTINGS are two pages of one unsaved state: going from one to the other
        // keeps the edits for the one SAVE.
        var fromSettings = IsSettings(_panel);
        ShowPanel(panel);
        StatusText.Text = "";
        Dispatcher.UIThread.Post(() => PanelControl(panel)?.BringIntoView(), DispatcherPriority.Background);
        if (panel == Panel.Content)
        {
            ContentLines.Children.Clear();
            LoadContentPanel();
        }
        else if (panel == Panel.Controls) LoadControlsPanel();
        else if (panel == Panel.Wheel) LoadWheelPanel();
        else if (fromSettings) BuildSettingsPanel(_settingsStatus?.Text ?? "");
        else LoadSettingsPanel();
    }

    private Control? PanelControl(Panel panel) => panel switch
    {
        Panel.Content => ContentPanel,
        Panel.Graphics or Panel.Settings => SettingsPanel,
        Panel.Controls => ControlsPanel,
        Panel.Wheel => WheelPanel,
        _ => null,
    };

    private void ContentButton_Click(object? sender, RoutedEventArgs e) => OpenPanel(Panel.Content);
    private void GraphicsButton_Click(object? sender, RoutedEventArgs e) => OpenPanel(Panel.Graphics);
    private void SettingsButton_Click(object? sender, RoutedEventArgs e) => OpenPanel(Panel.Settings);
    private void ControlsButton_Click(object? sender, RoutedEventArgs e) => OpenPanel(Panel.Controls);
    private void WheelButton_Click(object? sender, RoutedEventArgs e) => OpenPanel(Panel.Wheel);

    /// <summary>--snapshot FILE [STATE] [WIDTHxHEIGHT]: render the window to a PNG and exit, to
    /// check a layout without clicking. STATE is setup, building, ready, content, graphics, settings, controls, wheel,
    /// crash, failure or playing; each fills its panel with sample data where the backend has
    /// none.</summary>
    public void Snapshot(string path, string? state, string? size)
    {
        if (size?.Split('x') is [var width, var height] && int.TryParse(width, out var w) && int.TryParse(height, out var h))
        {
            Width = w;
            Height = h;
        }
        Opened += async (_, _) =>
        {
            for (var i = 0; i < 600 && _mode == Mode.Loading && _host is null && PrimaryButton.IsVisible; i++)
                await Task.Delay(100);
            await Task.Delay(500);
            if (_host is not null) await ShowSnapshotStateAsync(state);
            await Task.Delay(400);
            var pixels = new Avalonia.PixelSize((int)Bounds.Width, (int)Bounds.Height);
            using var bitmap = new Avalonia.Media.Imaging.RenderTargetBitmap(pixels);
            bitmap.Render(this);
            bitmap.Save(path);
            Close();
        };
    }

    private async Task ShowSnapshotStateAsync(string? state)
    {
        switch (state)
        {
            case "ready":
                ShowMode(Mode.Ready);
                break;
            case "building":
                ShowMode(Mode.Building);
                Progress.Value = 47;
                PercentText.Text = "47%";
                ProgressMessage.Text = "codegen: Translating functions (8120/21400)";
                break;
            case "playing":
                ShowMode(Mode.Playing);
                break;
            case "content" or "graphics" or "settings" or "controls" or "wheel":
                ShowMode(Mode.Ready);
                var panel = Enum.Parse<Panel>(state, ignoreCase: true);
                OpenPanel(panel);
                await Task.Delay(300);
                PanelControl(panel)?.BringIntoView();
                break;
            case "crash":
                ShowMode(Mode.Crashed);
                CrashIdText.Text = "fm4crash-v1-0123456789abcdef0123";
                break;
            case "failure":
                ShowFailure("THE BUILD STOPPED",
                    "The build failed (exit 1).\nbuild: ninja: build stopped: subcommand failed.\nLogs: data\\source\\0.1.0\\.local\\logs",
                    Mode.Setup);
                PrimaryButton.Content = "RETRY";
                break;
        }
    }
}
