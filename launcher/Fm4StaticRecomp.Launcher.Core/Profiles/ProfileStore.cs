// Copyright (c) 2026, i2kiselev
// BSD 3-Clause License: see LICENSE in the repository root.
using System.Text;
using Fm4StaticRecomp.Launcher.Core.Content;
using Fm4StaticRecomp.Launcher.Core.Install;
using Fm4StaticRecomp.Launcher.Core.Pfp4;
using Fm4StaticRecomp.Launcher.Core.Platform;
using Fm4StaticRecomp.Launcher.Core.Settings;
using Fm4StaticRecomp.Launcher.Core.Setup;

namespace Fm4StaticRecomp.Launcher.Core.Profiles;

public sealed class ProfileStore(InstallLayout layout, IPlatform platform, string installGame)
{
    // The title-wide (XUID 0) content folder the SDK installs Disc 2 and DLC into.
    public const string ContentFolderName = "0000000000000000";
    // The XUID the runtime signs in with; Project Forza Plus 4's Skip Intro saves were made with it too.
    public const string SaveXuid = "B13EBABEBABEBABE";
    public const string Pfp4 = "pfp4";
    private const string Vanilla = "vanilla";
    internal const string FileName = "profile.toml";
    private const string XeniaRepairedKey = "xenia_repaired";
    // "<profile id>/<xuid>" of the profile and Xenia save a repair copied.
    private const string XeniaCopyOfKey = "xenia_copy_of";

    public IReadOnlyList<Profile> List() =>
        Directory.Exists(layout.Profiles)
            ? Directory.GetDirectories(layout.Profiles)
                .Where(d => File.Exists(Path.Combine(d, FileName)))
                .Select(Read).OrderBy(p => p.Name, StringComparer.CurrentCultureIgnoreCase).ToList()
            : [];

    public Profile Create(string name)
    {
        var (mode, harderAi) = CurrentMode();
        return Create(name, mode, harderAi);
    }

    private Profile Create(string name, string? mode, bool? harderAi)
    {
        var id = UniqueId(name);
        var folder = Path.Combine(layout.Profiles, id);
        Directory.CreateDirectory(Path.Combine(folder, "user"));
        try
        {
            Write(folder, name.Trim(), GamertagFrom(name), null);
            Stamp(folder, mode, harderAi);
            LinkContent(folder);
        }
        catch
        {
            Directory.Delete(folder, recursive: true);
            throw;
        }
        return Read(folder);
    }

    public Profile Rename(string id, string name, string gamertag)
    {
        if (!IsValidGamertag(gamertag)) throw new ArgumentException($"{gamertag} is not a valid gamertag.");
        var profile = Get(id);
        Write(profile.Folder, name.Trim(), gamertag, profile.LastBackedUpVersion);
        return Read(profile.Folder);
    }

    public void MarkBackedUp(string id, string version)
    {
        var profile = Get(id);
        Write(profile.Folder, profile.Name, profile.Gamertag, version);
    }

    public void Delete(string id)
    {
        var profile = Get(id);
        Directory.CreateDirectory(layout.Deleted);
        Directory.Move(profile.Folder, Path.Combine(layout.Deleted, $"{id}-{DateTime.Now:yyyyMMdd-HHmmss}"));
    }

    // A new profile holding one of Project Forza Plus 4's Skip Intro saves (a cached
    // skip-intro folder), laid out as the game writes its own save, header included.
    public Profile CreateFromSkipIntro(string name, string saveFolder)
    {
        if (!Directory.Exists(saveFolder))
            throw new InvalidDataException(
                $"The Skip Intro save {Path.GetFileName(saveFolder)} is missing from the mod's saved files. {Pfp4Modes.ChooseTheModAgain}");
        var profile = Create(name);
        try
        {
            var title = Path.Combine(profile.User, SaveXuid, "4D530910");
            CopyDirectory(saveFolder, Path.Combine(title, "00000001"));
            var headers = Path.Combine(title, "Headers", "00000001");
            Directory.CreateDirectory(headers);
            foreach (var content in Directory.GetDirectories(saveFolder).Select(d => Path.GetFileName(d)))
                File.WriteAllBytes(Path.Combine(headers, content + ".header"), ContentHeader.Build(ContentHeader.SaveGame, "User", content, null));
        }
        catch
        {
            Remove(profile);
            throw;
        }
        return profile;
    }

    public void RecordMode(string id, string mode, bool harderAi) => Stamp(Get(id).Folder, mode, harderAi);

    // Null when the profile's mode is unknown (an imported save) or the same as the install's.
    // Harder AI changes only the AI's skills, not the career, so it warns of nothing.
    public static string? ModeWarning(Profile profile, string mode)
    {
        if (profile.Pfp4Mode is null || profile.Pfp4Mode == mode) return null;
        return $"This career was started in {Describe(profile.Pfp4Mode)}; this install now runs " +
               $"{Describe(mode)}. The career loads and plays, but its events and prices follow the current mode.";
    }

    private static string Describe(string mode) => $"{char.ToUpperInvariant(mode[0])}{mode[1..]} mode";

    // The game of a source that is one of our profiles (its profile.toml, beside the user folder),
    // null for any other save folder. A profile without `game` comes from a vanilla install.
    public static string? StampedGame(string sourceStateOrUser) =>
        StampFile(sourceStateOrUser) is { } file ? TomlFile.Load(file).Get("game") ?? Vanilla : null;

    private static string? StampFile(string source)
    {
        source = SourcePath.Resolve(source);
        var folder = Path.TrimEndingDirectorySeparator(Path.GetFullPath(source));
        var own = Path.Combine(folder, FileName);
        if (File.Exists(own)) return own;
        var parent = Path.Combine(Path.GetDirectoryName(folder)!, FileName);
        return Path.GetFileName(folder).Equals("user", StringComparison.OrdinalIgnoreCase) && File.Exists(parent) ? parent : null;
    }

    // The picker may point at a shared user/content folder holding several careers.
    // Return the individual save folders so the UI can let the player choose one.
    public static IReadOnlyList<string> ImportCandidates(string source)
    {
        source = SourcePath.Resolve(source);
        if (Directory.Exists(Path.Combine(source, "user"))) source = SourcePath.Resolve(Path.Combine(source, "user"));
        if (IsSave(source)) return [source];
        if (!Directory.Exists(source)) return [];
        var candidates = new List<string>();
        foreach (var entry in Directory.EnumerateFileSystemEntries(source))
        {
            try
            {
                var resolved = SourcePath.Resolve(entry);
                if (IsSave(resolved)) candidates.Add(resolved);
            }
            catch (Exception error) when (error is IOException or UnauthorizedAccessException)
            {
                // An unrelated broken shortcut must not hide the other saves.
            }
        }
        return candidates.Distinct(StringComparer.Ordinal).Order(StringComparer.Ordinal).ToList();
    }

    // Copies a state's (or a user folder's, or Xenia's content folder's) saves into a new profile.
    // Installed content is left behind: the profile links to the shared content instead. A source
    // that is one of our profiles is judged by its stamp; any other by the game the player declared.
    // A warning the player went past stays with the profile, for whoever reads its log after a problem.
    // A save made with another XUID than the runtime's (Xenia's) keeps it: the profile records it
    // for the game to sign in with, and Xenia Canary's headers are rewritten for the runtime.
    public Profile Import(string sourceStateOrUser, string name, string declaredGame, string? overriddenWarning = null)
    {
        sourceStateOrUser = SourcePath.Resolve(sourceStateOrUser);
        var user = Path.TrimEndingDirectorySeparator(Directory.Exists(Path.Combine(sourceStateOrUser, "user"))
            ? Path.Combine(sourceStateOrUser, "user")
            : sourceStateOrUser);
        // The copy would run into the profile it creates.
        if (IsUnder(layout.Profiles, user))
            throw new IOException($"{sourceStateOrUser} holds this launcher's own profiles. Choose another profile's folder or an older state folder.");
        // One Xenia profile's folder (content\<xuid>) chosen by itself.
        var single = IsSave(user);
        var saves = single ? [user] : Saves(user);
        if (saves.Count == 0)
            throw new IOException($"{sourceStateOrUser} holds no FM4 save. Choose a profile's folder, a state folder or Xenia Canary's content folder.");
        var stamp = StampFile(sourceStateOrUser) is { } stampFile ? Read(Path.GetDirectoryName(stampFile)!) : null;
        // One of our profiles moves whole: one imported from Xenia before 0.3.1 also holds the Xenia save
        // the game never read, beside the career it made under the runtime's XUID.
        if (stamp is null && saves.Count > 1)
            throw new IOException($"{sourceStateOrUser} holds the saves of {saves.Count} profiles " +
                                  $"({string.Join(", ", saves.Select(Path.GetFileName))}). Choose the folder of the one to import, inside it.");
        var xuid = stamp is null ? Path.GetFileName(saves[0]) : stamp.Xuid ?? SaveXuid;
        if (Refusal(stamp?.Game ?? declaredGame) is { } refusal) throw new IOException(refusal);
        var profile = Create(name, stamp?.Pfp4Mode, stamp?.Pfp4HarderAi);
        try
        {
            foreach (var directory in single ? [user] : Directory.GetDirectories(user))
            {
                var folderName = Path.GetFileName(directory);
                if (folderName.Equals(ContentFolderName, StringComparison.OrdinalIgnoreCase)) continue;
                // Xenia's other profiles (other games' saves, their accounts) stay behind.
                if (stamp is null && IsXuid(folderName) && !folderName.Equals(xuid, StringComparison.OrdinalIgnoreCase)) continue;
                CopyDirectory(directory, Path.Combine(profile.User, folderName));
            }
            if (!single)
                foreach (var file in Directory.GetFiles(user)) File.Copy(file, Path.Combine(profile.User, Path.GetFileName(file)));
            RewriteContainerHeaders(Path.Combine(profile.User, xuid, "4D530910", "Headers"));
            var path = Path.Combine(profile.Folder, FileName);
            var toml = TomlFile.Load(path);
            if (overriddenWarning is not null) toml.Set("import_warning", overriddenWarning, quoted: true);
            if (!xuid.Equals(SaveXuid, StringComparison.OrdinalIgnoreCase)) toml.Set("xuid", xuid, quoted: true);
            if (StampFile(sourceStateOrUser) is { } repaired && TomlFile.Load(repaired).Get(XeniaRepairedKey) == "true")
                toml.Set(XeniaRepairedKey, "true", quoted: false);
            toml.Save(path);
        }
        catch
        {
            Remove(profile);
            throw;
        }
        return Read(profile.Folder);
    }

    // 0.3.0 imported a Xenia save as it was: no xuid recorded and Canary's headers kept, so the game,
    // signed in with the runtime's XUID, never read it. Once per profile: without a career of the runtime's
    // beside it, the profile signs in with the Xenia XUID; with one, each Xenia career gets a profile of its
    // own and the career played since stays where it is. One line per repair, for the log.
    public IReadOnlyList<string> RepairXeniaImports()
    {
        var lines = new List<string>();
        var profiles = List();
        foreach (var profile in profiles)
        {
            try
            {
                var path = Path.Combine(profile.Folder, FileName);
                var toml = TomlFile.Load(path);
                if (profile.Xuid is not null || toml.Get(XeniaRepairedKey) == "true" || toml.Get(XeniaCopyOfKey) is not null) continue;
                var saves = Saves(profile.User);
                var xenia = saves.Where(s => !Path.GetFileName(s).Equals(SaveXuid, StringComparison.OrdinalIgnoreCase)).ToList();
                if (xenia.Count == 0) continue;
                if (xenia.Count == 1 && saves.Count == 1)
                {
                    var xuid = Path.GetFileName(xenia[0]);
                    RewriteContainerHeaders(Path.Combine(xenia[0], "4D530910", "Headers"));
                    toml.Set("xuid", xuid, quoted: true);
                    toml.Save(path);
                    lines.Add($"Profile {profile.Name}: the Xenia save imported before 0.3.1 now loads (signs in as {xuid}).");
                    continue;
                }
                foreach (var save in xenia)
                {
                    var xuid = Path.GetFileName(save);
                    var source = $"{profile.Id}/{xuid}";
                    // A copy cut short at an earlier start has no xuid yet, which is written last: it is made again.
                    if (profiles.FirstOrDefault(p => TomlFile.Load(Path.Combine(p.Folder, FileName)).Get(XeniaCopyOfKey) == source) is { } earlier)
                    {
                        if (earlier.Xuid is not null) continue;
                        Remove(earlier);
                    }
                    var name = xenia.Count == 1 ? $"{profile.Name} (Xenia)" : $"{profile.Name} (Xenia {xuid})";
                    var copy = Create(name, profile.Pfp4Mode, profile.Pfp4HarderAi);
                    try
                    {
                        var copyPath = Path.Combine(copy.Folder, FileName);
                        var copyToml = TomlFile.Load(copyPath);
                        copyToml.Set(XeniaCopyOfKey, source, quoted: true);
                        copyToml.Set("gamertag", profile.Gamertag, quoted: true);
                        copyToml.Set("game", profile.Game, quoted: true);
                        if (profile.ImportWarning is { } warning) copyToml.Set("import_warning", warning, quoted: true);
                        copyToml.Save(copyPath);
                        CopyDirectory(save, Path.Combine(copy.User, xuid));
                        RewriteContainerHeaders(Path.Combine(copy.User, xuid, "4D530910", "Headers"));
                        copyToml.Set("xuid", xuid, quoted: true);
                        copyToml.Save(copyPath);
                    }
                    catch
                    {
                        Remove(copy);
                        throw;
                    }
                    lines.Add($"Profile {profile.Name}: the Xenia save imported before 0.3.1 is now the profile {name}.");
                }
                toml.Set(XeniaRepairedKey, "true", quoted: false);
                toml.Save(path);
            }
            catch (Exception error) when (error is IOException or UnauthorizedAccessException)
            {
                lines.Add($"Profile {profile.Name}: the Xenia save imported before 0.3.1 could not be repaired: {error.Message}");
            }
        }
        return lines;
    }

    // Why saves of this game cannot be imported here; null when they can.
    public string? Refusal(string game) =>
        game == installGame ? null
        : installGame == Pfp4 ? "This is a Project Forza Plus 4 install; vanilla saves cannot be used here. Import them into a vanilla install."
        : "This is a vanilla install; Project Forza Plus 4 saves cannot be used here. Import them into a Project Forza Plus 4 install.";

    private static void Remove(Profile profile)
    {
        // The link first: a recursive delete does not remove a junction it meets.
        Directory.Delete(Path.Combine(profile.User, ContentFolderName));
        Directory.Delete(profile.Folder, recursive: true);
    }

    private (string? Mode, bool? HarderAi) CurrentMode()
    {
        if (installGame != Pfp4) return (null, null);
        var state = SetupState.Load(layout.SetupState);
        return (state.Pfp4Mode, state.Pfp4HarderAi);
    }

    internal static bool HoldsSave(string user) => Saves(user).Count > 0;

    private static List<string> Saves(string user)
    {
        if (Directory.Exists(user)) return Directory.GetDirectories(user).Where(IsSave).ToList();
        // Unix reports a dangling directory link as missing; surface it just as
        // an unreadable Windows profile rather than silently skipping repairs.
        if (new DirectoryInfo(user).LinkTarget is not null)
            throw new DirectoryNotFoundException($"The profile save folder {user} points to an unavailable directory.");
        return [];
    }

    // A save is a 16-digit XUID folder with FM4's title folder in it.
    internal static bool IsSave(string folder) =>
        Path.GetFileName(Path.TrimEndingDirectorySeparator(folder)) is var xuid && IsXuid(xuid) &&
        !xuid.Equals(ContentFolderName, StringComparison.OrdinalIgnoreCase) && Directory.Exists(Path.Combine(folder, "4D530910"));

    private static bool IsXuid(string name) => name.Length == 16 && name.All(char.IsAsciiHexDigit);

    // Headers\<content type>\<file name>.header, as Xenia Canary left them.
    private static void RewriteContainerHeaders(string headers)
    {
        if (!Directory.Exists(headers)) return;
        foreach (var typeFolder in Directory.GetDirectories(headers))
        {
            if (!uint.TryParse(Path.GetFileName(typeFolder), System.Globalization.NumberStyles.HexNumber, null, out var type)) continue;
            foreach (var header in Directory.GetFiles(typeFolder, "*.header"))
                if (ContentHeader.FromContainer(File.ReadAllBytes(header), type, Path.GetFileNameWithoutExtension(header)) is { } own)
                    File.WriteAllBytes(header, own);
        }
    }

    internal static bool IsUnder(string path, string folder)
    {
        var prefix = Path.TrimEndingDirectorySeparator(Path.GetFullPath(folder)) + Path.DirectorySeparatorChar;
        return (Path.TrimEndingDirectorySeparator(Path.GetFullPath(path)) + Path.DirectorySeparatorChar)
            .StartsWith(prefix, StringComparison.OrdinalIgnoreCase);
    }

    public static bool IsValidGamertag(string gamertag) =>
        gamertag.Length is >= 1 and <= 15 && char.IsAsciiLetter(gamertag[0]) &&
        gamertag.All(c => char.IsAsciiLetterOrDigit(c) || c == ' ');

    public Profile Get(string id) => List().SingleOrDefault(p => p.Id == id) ?? throw new KeyNotFoundException(id);

    private void LinkContent(string folder)
    {
        Directory.CreateDirectory(layout.Content);
        platform.CreateDirectoryLink(Path.Combine(folder, "user", ContentFolderName), layout.Content);
    }

    public void EnsureContentLink(Profile profile) =>
        EnsureContentLink(platform, Path.Combine(profile.User, ContentFolderName), layout.Content);

    // A junction holds an absolute path, so a launcher folder moved since still links to the old
    // content, and one copied in Explorer has an empty folder in the link's place. A folder holding
    // files is someone's own content and stays.
    public static void EnsureContentLink(IPlatform platform, string link, string content)
    {
        if (new DirectoryInfo(link).LinkTarget is { } target)
        {
            if (SamePath(target, content)) return;
            // Unix distinguishes a dangling symlink from an existing directory.
            if (!OperatingSystem.IsWindows() && !Directory.Exists(link)) File.Delete(link);
            else Directory.Delete(link);
        }
        else if (Directory.Exists(link))
        {
            if (Directory.EnumerateFileSystemEntries(link).Any()) return;
            Directory.Delete(link);
        }
        Directory.CreateDirectory(content);
        platform.CreateDirectoryLink(link, content);
    }

    internal static bool SamePath(string a, string b) =>
        string.Equals(Path.TrimEndingDirectorySeparator(Path.GetFullPath(a)),
            Path.TrimEndingDirectorySeparator(Path.GetFullPath(b)), StringComparison.OrdinalIgnoreCase);

    private static Profile Read(string folder)
    {
        var file = TomlFile.Load(Path.Combine(folder, FileName));
        var id = Path.GetFileName(folder);
        return new Profile(id, folder, file.Get("name") ?? id, file.Get("gamertag") ?? "Player",
            file.Get("last_backed_up_version"), file.Get("game") ?? Vanilla, file.Get("pfp4_mode"),
            bool.TryParse(file.Get("pfp4_harder_ai"), out var harderAi) ? harderAi : null, file.Get("import_warning"),
            file.Get("xuid"));
    }

    private void Stamp(string folder, string? mode, bool? harderAi)
    {
        var path = Path.Combine(folder, FileName);
        var file = TomlFile.Load(path);
        file.Set("game", installGame, quoted: true);
        if (mode is null) file.Remove("pfp4_mode");
        else file.Set("pfp4_mode", mode, quoted: true);
        if (harderAi is null) file.Remove("pfp4_harder_ai");
        else file.Set("pfp4_harder_ai", harderAi.Value ? "true" : "false", quoted: false);
        file.Save(path);
    }

    private static void Write(string folder, string name, string gamertag, string? backedUp)
    {
        var path = Path.Combine(folder, FileName);
        var file = TomlFile.Load(path);
        file.Set("name", name, quoted: true);
        file.Set("gamertag", gamertag, quoted: true);
        if (backedUp is not null) file.Set("last_backed_up_version", backedUp, quoted: true);
        file.Save(path);
    }

    private string UniqueId(string name)
    {
        var builder = new StringBuilder();
        foreach (var c in name.ToLowerInvariant())
            builder.Append(char.IsAsciiLetterOrDigit(c) ? c : '-');
        var id = string.Join('-', builder.ToString().Split('-', StringSplitOptions.RemoveEmptyEntries));
        if (id.Length == 0) id = "profile";
        var candidate = id;
        for (var n = 2; Directory.Exists(Path.Combine(layout.Profiles, candidate)); n++) candidate = $"{id}-{n}";
        return candidate;
    }

    internal static string GamertagFrom(string name)
    {
        var builder = new StringBuilder();
        foreach (var c in name)
            builder.Append(char.IsAsciiLetterOrDigit(c) ? c : ' ');
        var tag = string.Join(' ', builder.ToString().Split(' ', StringSplitOptions.RemoveEmptyEntries));
        tag = tag.Length > 15 ? tag[..15].TrimEnd() : tag;
        return IsValidGamertag(tag) ? tag : "Player";
    }

    internal static void CopyDirectory(string source, string target)
    {
        Directory.CreateDirectory(target);
        foreach (var file in Directory.GetFiles(source)) File.Copy(file, Path.Combine(target, Path.GetFileName(file)));
        foreach (var directory in Directory.GetDirectories(source))
            if (new DirectoryInfo(directory).LinkTarget is null)
                CopyDirectory(directory, Path.Combine(target, Path.GetFileName(directory)));
    }
}
