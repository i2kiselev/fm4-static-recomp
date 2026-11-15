# Profiles and saves

[← Back to the README](../README.md)

Each launcher profile is a separate save: its own career, garage and gamertag. Disc 2, DLC and
the graphics settings are shared by every profile of an install. Pick the profile in the list
beside **PLAY**; the **PROFILE** button manages them.

## Profiles

| PROFILE → | What it does |
|---|---|
| **NEW** | A new profile with a new career. Its gamertag starts as its name. |
| **RENAME** | Changes the name and the gamertag (1 to 15 English letters, digits or spaces, starting with a letter). |
| **OPEN SAVE FOLDER** | Shows the profile's folder in Explorer. |
| **IMPORT SAVE** | Copies saves from another place into a new profile ([below](#importing-saves)). |
| **IMPORT XBOX 360 SAVE** | Copies a career from an Xbox 360 save file into a new profile ([below](#a-career-from-an-xbox-360)). |
| **DELETE** | Moves the profile to `deleted\` in the saves folder after you type its name; you can restore it from there by hand. |

Before a new version of the game first runs a profile, the launcher zips that profile's save to
`backups\` in the saves folder (`data\backups\` by default); it keeps the newest three per profile.

## Importing saves

**PROFILE → IMPORT SAVE** takes one of these folders and copies its saves into a new profile; the
folder itself is not changed:

- a profile's folder from another launcher install (one of the folders in its `data\profiles\`,
  or in its saves folder's `profiles\`), for example of an old install you replaced;
- a manual install's state folder (`.local\state`, or the folder you passed as `-StateRoot`);
- Xenia Canary's `content` folder ([below](#a-career-from-xenia-canary)).

The launcher then asks whether the saves are vanilla or
[Project Forza Plus 4](project-forza-plus.md) ones (a profile of this launcher answers by itself)
and refuses saves of the other kind of install. Disc 2 and DLC are not copied: install them in
**CONTENT**.

### A career from Xenia Canary

1. Close Xenia.
2. **PROFILE → IMPORT SAVE**, and choose Xenia's `content` folder: the one beside
   `xenia_canary.exe`, or `Documents\xenia\content`. If several Xenia profiles have FM4 saves,
   open `content` and choose the folder of yours instead (16 letters and digits, such as
   `E030000047B76145`).
3. Answer **VANILLA SAVE** (or **PFP4 SAVE** for a Project Forza Plus 4 career) and name the
   profile.

The game accepts a save only from the Xbox profile it was made with, so the new profile plays as
that Xenia profile. Xenia's copy is not changed. Install your Disc 2 and DLC in **CONTENT**.
Only Xenia Canary saves are supported; **Xenia Edge** saves are not supported yet.

A Xenia save imported with version 0.3.0 or older did not load (the game started a new career instead).
The launcher repairs it once, at its first start after the update: the profile then loads the Xenia
career, or, if you played a new career in that profile meanwhile, the Xenia career gets a profile of its
own, named like the old one with `(Xenia)` after it, and the new career stays where it is. Pick the
new profile in the list beside PLAY. A profile that held saves of several Xenia profiles gets one new
profile for each, named with that Xenia profile's 16 letters and digits.

### A career from an Xbox 360

FM4 on the Xbox 360 keeps the career in one file, `ForzaProfile`. Copy it to a USB drive on the
console, then export it on the PC with a tool such as Horizon or Velocity, as one file. Don't extract
it: the files inside load only together with the Xbox profile named in the file itself.

1. **PROFILE → IMPORT XBOX 360 SAVE**, and choose the file.
2. Answer **VANILLA SAVE** and name the profile (it starts as the Xbox gamertag).

The new profile plays as that Xbox profile. The file is not changed.

What does not work:

- **Extracted files** (`ForzaProfile`, `GameplayLog`, `PlayerDatabase`, `Liveries` and so on, taken out
  of the save with Horizon, Velocity, wxPirs or similar): they don't say which Xbox profile made them.
  Export the save again as one file.
- **A whole USB drive or hard drive** (the `Xbox360\Data0000`… files): open it in one of these tools and
  export the FM4 save from it.
- **A save moved to another profile** with a modding tool (for example, Horizon's profile ID change): the
  game still finds it "tampered with". Use the save as the console made it.
- **Cloud saves**: download the save to the console first, then copy it to a USB drive.

Saves exported with Horizon are tested; other tools that export the save unchanged as one file should
work the same way.

## The saves folder

SETTINGS → ADVANCED → **SAVES FOLDER** keeps your saves outside `data\`, for example in a folder
you back up or sync, or one that stays when you delete the launcher's folder. **CHOOSE…** a
folder, then **SAVE**: the launcher moves its profiles, their backups and deleted profiles there
(as `profiles\`, `backups\` and `deleted\`). **DEFAULT** moves them back into `data\`. Disc 2 and
DLC stay in `data\`.

- **After a reinstall**, choose the saves folder you used before: its profiles appear again. The
  new install's own profiles are added beside them; one of the same name gets a number, "Main (2)",
  and an empty one that setup just made (no save yet) goes to `deleted\` instead. If your saves
  were in the old install's `data\` folder, that folder is refused (it belongs to the old
  launcher): bring each profile over with **PROFILE → IMPORT SAVE**.
- The folder must be on a drive inside this PC: a network drive or a USB stick formatted
  FAT32 or exFAT cannot hold the saves.
- Choose the folder that holds `profiles\`, not `profiles\` itself.
- Moving the saves away from a folder removes them there, also from a synced folder on your other
  PCs.
- If the saves folder is missing when the launcher starts (its drive not connected, or the folder
  renamed), the launcher says so and makes no new profile until it is back or you choose another.

## Vanilla and Project Forza Plus 4 saves don't mix

Each profile records which game it was made on. IMPORT SAVE refuses saves of the other game, and
SAVES FOLDER warns before it takes over a folder that holds them: they are not guaranteed to work
there, and starting a fresh career is recommended. To play both, give each install its own saves
folder. Why: [Saves](project-forza-plus-internals.md#saves) in the mod's technical page.

## Garage pictures

The game draws a car's picture in the garage once, when the car arrives, and keeps it in the save.
A save from Xenia brings Xenia's pictures along, and a picture Xenia drew wrong (noise, no shadow
under the car) stays wrong here too. Choose that car, then **PAINT → PAINT CAR**, repaint the body
once and save: the game draws the picture again.
