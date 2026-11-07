// Copyright (c) 2026, i2kiselev
// BSD 3-Clause License: see LICENSE in the repository root.
using Avalonia.Controls;
using Avalonia.Interactivity;
using Fm4StaticRecomp.Launcher.Core.Profiles;

namespace Fm4StaticRecomp.Launcher;

// The profile picker beside Play and its menu (spec 7): one save each, switched before Play.
public partial class MainWindow
{
    private bool _loadingProfiles;

    private Profile? SelectedProfile => ProfileCombo.SelectedItem is ComboBoxItem { Tag: Profile profile } ? profile : null;

    private void ReloadProfiles()
    {
        if (_host is not { } host) return;
        _loadingProfiles = true;
        var profiles = host.Profiles.List();
        ProfileCombo.Items.Clear();
        foreach (var profile in profiles)
            ProfileCombo.Items.Add(new ComboBoxItem { Content = profile.Name, Tag = profile });
        var active = profiles.FirstOrDefault(p => p.Id == host.Settings.ActiveProfile) ?? profiles.FirstOrDefault();
        ProfileCombo.SelectedIndex = active is null ? -1 : profiles.ToList().IndexOf(active);
        _loadingProfiles = false;
        // A saves folder on a drive that is not connected, or renamed in Explorer, lists no profiles.
        if (host.SavesFolderMissing) StatusText.Text = SavesFolderMissingText(host);
        if (active is not null && host.Settings.ActiveProfile != active.Id) SaveActiveProfile(active);
        UpdateActions();
    }

    private void SaveActiveProfile(Profile profile)
    {
        _host!.Settings.ActiveProfile = profile.Id;
        _host.Settings.Save();
    }

    private void ProfileCombo_Changed(object? sender, SelectionChangedEventArgs e)
    {
        if (_loadingProfiles || _host is null || SelectedProfile is not { } profile || _host.GameRunning) return;
        if (_host.Settings.ActiveProfile != profile.Id) SaveActiveProfile(profile);
        UpdateActions();
    }

    private static string SavesFolderMissingText(LauncherHost host) =>
        $"The saves folder {host.Settings.SavesFolder} cannot be found. Connect its drive, or choose another in SETTINGS → SAVES FOLDER.";

    private async Task ProfileActionAsync(Func<LauncherHost, Task> action)
    {
        if (_host is not { } host || host.GameRunning || _busy) return;
        // A profile made now would start an empty saves folder where the player's own one belongs.
        if (host.SavesFolderMissing)
        {
            await Dialog.AskAsync(this, "PROFILE", SavesFolderMissingText(host), "OK");
            return;
        }
        try
        {
            await action(host);
        }
        catch (Exception error) when (error is IOException or UnauthorizedAccessException or InvalidDataException)
        {
            AppendLog($"Profile: {error.Message}");
            await Dialog.AskAsync(this, "PROFILE", error.Message, "OK");
        }
        ReloadProfiles();
    }

    private async void NewProfile_Click(object? sender, RoutedEventArgs e) => await ProfileActionAsync(async host =>
    {
        var (answer, values) = await Dialog.PromptAsync(this, "NEW PROFILE",
            "A separate save: its own career, garage and gamertag. Disc 2, DLC and graphics settings are shared.",
            [("Name", "")], "CREATE", "CANCEL");
        if (answer != "CREATE" || values[0].Trim().Length == 0) return;
        string? save = null;
        if (host.Setup.Game == ProfileStore.Pfp4)
        {
            var (chosen, folder) = await AskStartAsync(host, host.Settings.ActiveVersion ?? host.Payload.Version, offerNewCareer: true);
            if (!chosen) return;
            save = folder;
        }
        var profile = save is null ? host.Profiles.Create(values[0]) : host.Profiles.CreateFromSkipIntro(values[0], save);
        SaveActiveProfile(profile);
    });

    private async void RenameProfile_Click(object? sender, RoutedEventArgs e) => await ProfileActionAsync(async host =>
    {
        if (SelectedProfile is not { } profile) return;
        var (name, gamertag) = (profile.Name, profile.Gamertag);
        var message = "";
        while (true)
        {
            var (answer, values) = await Dialog.PromptAsync(this, "RENAME PROFILE", message,
                [("Name", name), ("Gamertag", gamertag)], "SAVE", "CANCEL");
            if (answer != "SAVE") return;
            (name, gamertag) = (values[0], values[1].Trim());
            if (name.Trim().Length > 0 && ProfileStore.IsValidGamertag(gamertag)) break;
            message = "A gamertag has 1 to 15 English letters, digits or spaces and starts with a letter; the name cannot be empty.";
        }
        host.Profiles.Rename(profile.Id, name, gamertag);
    });

    private void OpenSaveFolder_Click(object? sender, RoutedEventArgs e)
    {
        if (SelectedProfile is not { } profile) return;
        Directory.CreateDirectory(profile.User);
        LauncherHost.Open(profile.User);
    }

    private async void ImportSave_Click(object? sender, RoutedEventArgs e) => await ProfileActionAsync(async host =>
    {
        if (await PickSaveFolderAsync() is not { } folder) return;
        if (await AskSaveGameAsync(folder) is not var (game, how, warning)) return;
        if (host.Profiles.Refusal(game) is { } refusal) throw new IOException(refusal);
        var (answer, values) = await Dialog.PromptAsync(this, "IMPORT SAVE",
            $"Copies the saves in {folder} into a new profile. The folder is not changed.",
            [("Name", Path.GetFileName(Path.TrimEndingDirectorySeparator(folder)))], "IMPORT", "CANCEL");
        if (answer != "IMPORT" || values[0].Trim().Length == 0) return;
        SetBusy(true);
        try
        {
            var profile = await Task.Run(() => host.Profiles.Import(folder, values[0], game, warning));
            AppendLog($"Profile {profile.Name} imported from {folder} as {GameName(game)} saves ({how})." +
                      (warning is null ? "" : $" Imported past a warning: {warning}."));
            SaveActiveProfile(profile);
        }
        finally
        {
            SetBusy(false);
        }
    });

    private async Task<string?> PickSaveFolderAsync()
    {
        if (await PickFolderAsync("Choose a save folder, user folder or its alias") is not { } folder) return null;
        // Our stamped profile folders must stay whole when imported.
        if (ProfileStore.StampedGame(folder) is not null) return folder;
        var candidates = await Task.Run(() => ProfileStore.ImportCandidates(folder));
        if (candidates.Count == 1) return candidates[0];
        if (candidates.Count == 0)
        {
            await Dialog.AskAsync(this, "NO FM4 SAVE FOUND",
                $"{folder} holds no FM4 save. Choose its Xbox profile folder, user folder or an alias pointing to one.", "OK");
            return null;
        }
        var choices = candidates.Select((p, i) => $"{i + 1}. {Path.GetFileName(p)}").ToList();
        var selected = await Dialog.ChooseAsync(this, "CHOOSE YOUR SAVE",
            $"{folder} contains several FM4 saves. Choose the Xbox profile to copy; the original stays in place.",
            choices, "CANCEL");
        return selected is null or "CANCEL" ? null : candidates[choices.IndexOf(selected)];
    }

    // Which game a save is from: our profile's stamp says it; for any other folder the player
    // does, with no answer preselected, and a DLC folder beside the saves that says otherwise warns.
    // How it was decided and the warning the player went past are returned for the log.
    private async Task<(string Game, string How, string? Warning)?> AskSaveGameAsync(string folder)
    {
        if (ProfileStore.StampedGame(folder) is { } stamped) return (stamped, "its profile's stamp", null);
        var answer = await Dialog.AskAsync(this, "WHICH GAME ARE THESE SAVES FROM?",
            "Saves of vanilla Forza Motorsport 4 and of Project Forza Plus 4 cannot be used in each other's installs: " +
            "a vanilla career on the mod hangs on parts the mod removed.",
            "CANCEL", "VANILLA SAVE", "PFP4 SAVE");
        if (answer is not ("VANILLA SAVE" or "PFP4 SAVE")) return null;
        var declared = answer == "PFP4 SAVE" ? ProfileStore.Pfp4 : "vanilla";
        var guess = await Task.Run(() => SaveOrigin.Guess(folder));
        if (guess is null || guess == declared) return (declared, guess is null ? "the player's answer" : "the player's answer, the DLC folder agrees", null);
        var anyway = $"IMPORT AS {GameName(declared).ToUpperInvariant()}";
        var sure = await Dialog.AskAsync(this, "ARE YOU SURE?",
            $"The DLC folder beside these saves is {GameName(guess)}'s, not {GameName(declared)}'s. " +
            $"Import them as {GameName(declared)} saves anyway?", "CANCEL", anyway);
        return sure == anyway
            ? (declared, "the player's answer", $"the DLC folder beside the saves is {GameName(guess)}'s")
            : null;
    }

    private static string GameName(string game) => game == ProfileStore.Pfp4 ? "Project Forza Plus 4" : "vanilla FM4";

    private async void DeleteProfile_Click(object? sender, RoutedEventArgs e) => await ProfileActionAsync(async host =>
    {
        if (SelectedProfile is not { } profile) return;
        var (answer, values) = await Dialog.PromptAsync(this, "DELETE PROFILE",
            $"Moves {profile.Name} to {host.Layout.Deleted}; it can be restored from there by hand. Type its name to confirm.",
            [("Name", "")], "DELETE", "CANCEL");
        if (answer != "DELETE" || values[0] != profile.Name) return;
        host.Profiles.Delete(profile.Id);
        if (host.Settings.ActiveProfile == profile.Id)
        {
            host.Settings.ActiveProfile = null;
            host.Settings.Save();
        }
    });
}
