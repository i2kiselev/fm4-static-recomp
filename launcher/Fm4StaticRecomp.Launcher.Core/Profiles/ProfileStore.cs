using System.Text;
using Fm4StaticRecomp.Launcher.Core.Install;
using Fm4StaticRecomp.Launcher.Core.Platform;
using Fm4StaticRecomp.Launcher.Core.Settings;

namespace Fm4StaticRecomp.Launcher.Core.Profiles;

public sealed class ProfileStore(InstallLayout layout, IPlatform platform)
{
    // The title-wide (XUID 0) content folder the SDK installs Disc 2 and DLC into.
    public const string ContentFolderName = "0000000000000000";
    private const string FileName = "profile.toml";

    public IReadOnlyList<Profile> List() =>
        Directory.Exists(layout.Profiles)
            ? Directory.GetDirectories(layout.Profiles)
                .Where(d => File.Exists(Path.Combine(d, FileName)))
                .Select(Read).OrderBy(p => p.Name, StringComparer.CurrentCultureIgnoreCase).ToList()
            : [];

    public Profile Create(string name)
    {
        var id = UniqueId(name);
        var folder = Path.Combine(layout.Profiles, id);
        Directory.CreateDirectory(Path.Combine(folder, "user"));
        try
        {
            Write(folder, name.Trim(), GamertagFrom(name), null);
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

    // Copies a state's (or a user folder's) saves into a new profile. Installed content is
    // left behind: the profile links to the shared content instead.
    public Profile Import(string sourceStateOrUser, string name)
    {
        var user = Directory.Exists(Path.Combine(sourceStateOrUser, "user"))
            ? Path.Combine(sourceStateOrUser, "user")
            : sourceStateOrUser;
        // The copy would run into the profile it creates.
        if (IsUnder(layout.Profiles, user))
            throw new IOException($"{sourceStateOrUser} holds this launcher's own profiles. Choose another profile's folder or an older state folder.");
        if (!HoldsSave(user))
            throw new IOException($"{sourceStateOrUser} holds no FM4 save. Choose a state folder or its user folder.");
        var profile = Create(name);
        try
        {
            foreach (var directory in Directory.GetDirectories(user))
            {
                var folderName = Path.GetFileName(directory);
                if (folderName.Equals(ContentFolderName, StringComparison.OrdinalIgnoreCase)) continue;
                CopyDirectory(directory, Path.Combine(profile.User, folderName));
            }
            foreach (var file in Directory.GetFiles(user)) File.Copy(file, Path.Combine(profile.User, Path.GetFileName(file)));
        }
        catch
        {
            // The link first: a recursive delete does not remove a junction it meets.
            Directory.Delete(Path.Combine(profile.User, ContentFolderName));
            Directory.Delete(profile.Folder, recursive: true);
            throw;
        }
        return profile;
    }

    // A save is a 16-digit XUID folder with FM4's title folder in it.
    private static bool HoldsSave(string user) =>
        Directory.Exists(user) && Directory.GetDirectories(user).Any(d =>
            Path.GetFileName(d) is { Length: 16 } xuid && xuid.All(char.IsAsciiHexDigit) &&
            !xuid.Equals(ContentFolderName, StringComparison.OrdinalIgnoreCase) && Directory.Exists(Path.Combine(d, "4D530910")));

    private static bool IsUnder(string path, string folder)
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
            Directory.Delete(link);
        }
        else if (Directory.Exists(link))
        {
            if (Directory.EnumerateFileSystemEntries(link).Any()) return;
            Directory.Delete(link);
        }
        Directory.CreateDirectory(content);
        platform.CreateDirectoryLink(link, content);
    }

    private static bool SamePath(string a, string b) =>
        string.Equals(Path.TrimEndingDirectorySeparator(Path.GetFullPath(a)),
            Path.TrimEndingDirectorySeparator(Path.GetFullPath(b)), StringComparison.OrdinalIgnoreCase);

    private static Profile Read(string folder)
    {
        var file = TomlFile.Load(Path.Combine(folder, FileName));
        var id = Path.GetFileName(folder);
        return new Profile(id, folder, file.Get("name") ?? id, file.Get("gamertag") ?? "Player",
            file.Get("last_backed_up_version"));
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

    private static void CopyDirectory(string source, string target)
    {
        Directory.CreateDirectory(target);
        foreach (var file in Directory.GetFiles(source)) File.Copy(file, Path.Combine(target, Path.GetFileName(file)));
        foreach (var directory in Directory.GetDirectories(source))
            if (new DirectoryInfo(directory).LinkTarget is null)
                CopyDirectory(directory, Path.Combine(target, Path.GetFileName(directory)));
    }
}
