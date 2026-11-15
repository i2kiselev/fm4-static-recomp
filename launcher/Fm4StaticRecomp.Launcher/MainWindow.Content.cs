// Portions copyright (c) 2026, Pinyon Shift contributors (BSD 3-Clause: see THIRD_PARTY_NOTICES.md).
// Copyright (c) 2026, i2kiselev (BSD 3-Clause: see LICENSE in the repository root).
// Adapted from Pinyon Shift's launcher (launcher/PinyonShift.Launcher.Desktop, BSD-3-Clause; see THIRD_PARTY_NOTICES.md).
using Avalonia.Controls;
using Avalonia.Interactivity;
using Avalonia.Platform.Storage;
using Fm4StaticRecomp.Launcher.Core.Content;
using Fm4StaticRecomp.Launcher.Core.Discs;

namespace Fm4StaticRecomp.Launcher;

// The Content panel (spec 6): Disc 2 and DLC, installed through the built game into the
// content folder every profile links to, and the unicorn cars.
public partial class MainWindow
{
    private const int MaximumContentLines = 40;

    private void LoadContentPanel()
    {
        if (_host is not { } host) return;
        // A Project Forza Plus 4 install has every package from the mod; nothing is added here.
        var pfp4 = Pfp4Install;
        Disc2StatusText.Text = pfp4 ? "From Project Forza Plus 4." : host.Setup.Disc2Installed ? "INSTALLED" : "Not installed.";
        Disc2AddButtons.IsVisible = !pfp4 && !host.Setup.Disc2Installed;
        DlcAddButtons.IsVisible = ContentDropHelp.IsVisible = !pfp4;
        ChooseModAgainButton.IsVisible = pfp4;
        var disc2Packages = Disc2PackageNames(host);
        var dlc = host.Content.Installed().Where(p => !disc2Packages.Contains(p.FileName)).ToList();
        DlcList.Children.Clear();
        if (dlc.Count == 0) DlcList.Children.Add(new TextBlock { Text = "None installed.", Classes = { "help" } });
        foreach (var package in dlc) DlcList.Children.Add(new TextBlock { Text = package.DisplayName });
        LoadUnicornStatus(host);
    }

    private void LoadUnicornStatus(LauncherHost host)
    {
        UnlockUnicornsButton.IsVisible = false;
        try
        {
            var report = UnicornCars.Read(host.Layout.Game);
            UnlockUnicornsButton.IsVisible = report.Locked.Count > 0;
            UnicornStatusText.Text = report.Locked.Count > 0
                ? $"{report.Locked.Count} of {UnicornCars.Known.Count} locked: the game keeps them out of the Autoshow."
                : $"Unlocked: {report.Unlocked.Count} can be bought in the Autoshow.";
        }
        catch (Exception error) when (error is IOException or UnauthorizedAccessException or InvalidDataException)
        {
            UnicornStatusText.Text = error.Message;
        }
    }

    private async void UnlockUnicorns_Click(object? sender, RoutedEventArgs e)
    {
        if (!ContentOpen || _host is not { } host) return;
        var answer = await Dialog.AskAsync(this, "UNLOCK UNICORN CARS",
            "Makes the unicorn cars buyable in the Autoshow for every profile of this install. " +
            @"The game's database is changed; its original is kept as data\game\Media\db\gamedb.before-unicorns.slt.",
            "UNLOCK", "CANCEL");
        if (answer != "UNLOCK" || !ContentOpen) return;
        await RunWorkAsync(_ => UnlockUnicornsAsync(host));
    }

    private async Task UnlockUnicornsAsync(LauncherHost host)
    {
        ContentLines.Children.Clear();
        try
        {
            var report = await Task.Run(() => UnicornCars.Unlock(host.Layout.Game));
            if (report.Locked.Count > 0) ContentLine($"Unlocked: {string.Join(", ", report.Locked.Select(c => c.Name))}.");
            if (report.Unlocked.Count > 0) ContentLine($"Already unlocked: {string.Join(", ", report.Unlocked.Select(c => c.Name))}.");
            var autovista = report.KeptLocked.Where(UnicornCars.HasOnlyAutovistaModel).ToList();
            if (autovista.Count > 0)
                ContentLine($"Left locked, the game has only their Autovista model: {string.Join(", ", autovista.Select(c => c.Name))}.");
            var others = report.KeptLocked.Except(autovista).ToList();
            if (others.Count > 0) ContentLine($"Left locked, not known unicorns: {string.Join(", ", others.Select(c => c.Name))}.");
        }
        catch (Exception error) when (error is IOException or UnauthorizedAccessException or InvalidDataException)
        {
            ContentLine(error.Message);
        }
        finally
        {
            LoadContentPanel();
        }
    }

    private static HashSet<string> Disc2PackageNames(LauncherHost host)
    {
        try { return host.Manifest().Disc2.Packages.Select(p => p.Name).ToHashSet(StringComparer.OrdinalIgnoreCase); }
        catch (Exception error) when (error is IOException or InvalidDataException) { return []; }
    }

    private void ContentLine(string text)
    {
        if (text.Length == 0) return;
        ContentLines.Children.Add(new TextBlock { Text = text, Classes = { "help" }, TextWrapping = Avalonia.Media.TextWrapping.Wrap });
        while (ContentLines.Children.Count > MaximumContentLines) ContentLines.Children.RemoveAt(0);
        AppendLog($"Content: {text}");
    }

    // Not during an update's build either: both use the one cancellable work slot that
    // closing the window waits for.
    private bool ContentOpen => _host is { GameRunning: false, Settings.ActiveVersion: not null } && !_busy && !_versionBuilding;

    private async void AddDisc2_Click(object? sender, RoutedEventArgs e)
    {
        if (ContentOpen && await PickFileAsync("Choose your FM4 Disc 2 image") is { } path) await RunWorkAsync(t => AddDisc2Async(path, t));
    }

    // The only way back to the mod's files once its saved modes or Skip Intro saves are missing
    // or damaged: setup then runs the mod step alone, since Disc 1 and the build are done.
    private async void ChooseModAgain_Click(object? sender, RoutedEventArgs e)
    {
        if (!ContentOpen || _host is not { } host) return;
        var answer = await Dialog.AskAsync(this, "CHOOSE THE MOD AGAIN",
            "Setup asks for Project Forza Plus 4 1.3's zip or folder and copies it over the game again. " +
            "The game does not start until that is done; profiles and settings stay.", "CHOOSE THE MOD", "CANCEL");
        if (answer != "CHOOSE THE MOD" || !ContentOpen) return;
        host.Setup.Pfp4Applied = false;
        host.Setup.Save();
        AppendLog("Project Forza Plus 4: the mod is to be chosen again.");
        ShowMode(Mode.Setup);
        Headline.Text = "CHOOSE THE MOD AGAIN";
        Subhead.Text = "Choose Project Forza Plus 4 1.3's zip or folder, then start setup.";
        StatusText.Text = "";
    }

    private async void AddDisc2Folder_Click(object? sender, RoutedEventArgs e)
    {
        if (ContentOpen && await PickFolderAsync("Choose your FM4 Disc 2 GOD folder or the folder with its packages") is { } path)
            await RunWorkAsync(t => AddDisc2Async(path, t));
    }

    private async void AddDlcFiles_Click(object? sender, RoutedEventArgs e)
    {
        if (!ContentOpen) return;
        var files = await StorageProvider.OpenFilePickerAsync(new FilePickerOpenOptions
            { Title = "Choose your FM4 DLC packages", AllowMultiple = true });
        var paths = files.Select(f => f.TryGetLocalPath()).OfType<string>().ToList();
        if (paths.Count > 0) await RunWorkAsync(t => AddDlcAsync(paths, t));
    }

    private async void AddDlcFolder_Click(object? sender, RoutedEventArgs e)
    {
        if (ContentOpen && await PickFolderAsync("Choose a folder with your FM4 DLC packages") is { } path)
            await RunWorkAsync(t => AddDlcAsync([path], t));
    }

    private async Task AddDisc2Async(string path, CancellationToken cancellation)
    {
        var host = _host!;
        var version = host.Settings.ActiveVersion!;
        ContentLines.Children.Clear();
        var staging = Path.Combine(host.Layout.Downloads, "disc2");
        try
        {
            var input = await Task.Run(() => DiscInput.DetectDisc2(path), cancellation);
            if (input is null)
            {
                ContentLine($"{DisplayName(path)} is not an FM4 Disc 2 image, GOD folder or packages folder.");
                return;
            }
            var manifest = await Task.Run(host.Manifest, cancellation);
            ContentLine($"Verifying {DisplayName(path)}.");
            var problems = await Disc2Source.VerifyAsync(input, manifest, null, cancellation);
            if (problems.Count > 0 &&
                await Dialog.AskAsync(this, "DISC 2 DIFFERS", ProblemList(problems, "These packages differ from the known ones:"),
                    "USE IT ANYWAY", "CANCEL") != "USE IT ANYWAY")
                return;
            ContentLine("Copying the packages.");
            var staged = await Disc2Source.StageAsync(input, manifest, staging, cancellation);
            var finished = await host.Content.InstallAsync(version, staging, null,
                new Progress<ContentEvent>(e => ContentLine(ContentText(e))), cancellation);
            if (!staged.Complete)
                ContentLine($"{staged.Problems.Count(p => p.Kind == FileProblemKind.Missing)} packages were missing: Disc 2 is not complete.");
            else if (finished.Failed == 0)
            {
                host.Setup.Disc2Installed = true;
                host.Setup.Disc2Source = null;
                host.Setup.Disc2Later = false;
                host.Setup.Save();
            }
        }
        catch (OperationCanceledException)
        {
            ContentLine("Stopped.");
        }
        catch (Exception error) when (error is IOException or UnauthorizedAccessException or InvalidDataException or InvalidOperationException)
        {
            ContentLine(error.Message);
        }
        finally
        {
            await DeleteQuietlyAsync(staging);
            LoadContentPanel();
        }
    }

    // Each file's package header decides: FM4's own DLC (LIVE or PIRS marketplace packages) is
    // copied into a staging folder and installed; any other package, a title update too, is
    // refused by name.
    private async Task AddDlcAsync(IReadOnlyList<string> paths, CancellationToken cancellation)
    {
        var host = _host!;
        var version = host.Settings.ActiveVersion!;
        ContentLines.Children.Clear();
        var staging = Path.Combine(host.Layout.Downloads, "dlc");
        try
        {
            var (accepted, refused, others) = await Task.Run(() => SortPackages(paths), cancellation);
            if (refused.Count > 0) ContentLine($"Not FM4 DLC, refused: {string.Join(", ", refused.Select(DisplayName))}");
            if (others > 0) ContentLine($"{others} files that are not packages were left out.");
            if (accepted.Count == 0)
            {
                ContentLine("No FM4 packages found.");
                return;
            }
            ContentLine($"Copying {accepted.Count} packages.");
            await Task.Run(() =>
            {
                if (Directory.Exists(staging)) Directory.Delete(staging, recursive: true);
                Directory.CreateDirectory(staging);
                foreach (var file in accepted)
                {
                    cancellation.ThrowIfCancellationRequested();
                    File.Copy(file, Path.Combine(staging, Path.GetFileName(file)), overwrite: true);
                }
            }, cancellation);
            await host.Content.InstallAsync(version, null, staging, new Progress<ContentEvent>(e => ContentLine(ContentText(e))),
                cancellation);
        }
        catch (OperationCanceledException)
        {
            ContentLine("Stopped.");
        }
        catch (Exception error) when (error is IOException or UnauthorizedAccessException or InvalidOperationException)
        {
            ContentLine(error.Message);
        }
        finally
        {
            await DeleteQuietlyAsync(staging);
            LoadContentPanel();
        }
    }

    private static (List<string> Accepted, List<string> Refused, int Others) SortPackages(IReadOnlyList<string> paths)
    {
        var options = new EnumerationOptions { RecurseSubdirectories = true, IgnoreInaccessible = true, AttributesToSkip = 0 };
        var files = paths.SelectMany(p => Directory.Exists(p) ? Directory.EnumerateFiles(p, "*", options) : [p]);
        List<string> accepted = [], refused = [];
        var others = 0;
        foreach (var file in files)
        {
            var header = PackageHeader.TryRead(file);
            if (header is null) others++;
            else if (header.IsFm4Dlc) accepted.Add(file);
            else refused.Add(file);
        }
        return (accepted, refused, others);
    }
}
