// Copyright (c) 2026, i2kiselev
// BSD 3-Clause License: see LICENSE in the repository root.
#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace fm4_static_recomp::decals {

// CUSTOM DECALS: the PNG images in a folder of the player's become decals of the game's livery
// editor, on every car, through items of its own in the layer menus: APPLY A CUSTOM DECAL (after
// APPLY A MANUFACTURER DECAL) and CHANGE CUSTOM DECAL (after CHANGE MANUFACTURER DECAL) open the
// game's decal picker with the custom decals alone; the game's own list stays as it ships. With
// ALL SPONSOR DECALS, every decal the game has is offered on every car. Both apply while the game
// runs.
//
// The picker lists a car's decals from the game database:
//   Livery_Decals JOIN Livery_DecalsSortOrder WHERE MakeID = <the car's maker> ORDER BY Sequence, AlphaSort
// (46 a maker: its own logo, the Forza logos, about 40 sponsors; 346 of the game's decals are on no
// list), and the livery system maps decal ids to files with
//   SELECT ID, Filename AS elemname FROM Livery_Decals ORDER BY ID
// each time a livery is first needed and each time the paint editor opens. Just before that query
// the decals' rows are brought up to date through the game's own database query (a hook); a custom
// item's choice is the game's decal choice with the picker's queries swapped for the custom decals'
// (hooks); names come from the string lookup (a hook); files from a content package the recomp
// keeps beside the player's DLC: the game mounts its folder at GAME:\ at startup and reads files
// added to it later. Nothing from the game is copied: the decal file layout is a game decal's,
// read from Media\Livery.zip as the game starts.

// A decal file (Media\Livery\Decals\decalNNN.carbin): a quad mesh carrying one square image of
// 32-bit ARGB with every level down to 1 pixel, the levels of at least 32 pixels Xbox 360 tiled. The
// game's are 256 x 256 (9 levels); the game takes 512 x 512 as well (its texture header has the size
// and level count), which custom decals use: four times the detail.
inline constexpr uint32_t kGameDecalSize = 256;
inline constexpr int kCustomDecalSize = 512;
inline constexpr size_t kDecalFileSize = 350075;  // a game decal
inline constexpr size_t kDecalPixelsAt = 0x153;

// True when `file` is a game decal (to take as the template for new decals).
bool IsDecalFile(const std::vector<uint8_t>& file);
// The size of a decal file of `size` x `size` pixels.
size_t DecalFileSize(int size);
// The decal file carrying `rgba` (`size` x `size`, a power of two of at least 32, 8-bit RGBA,
// straight alpha), made from `templ` (a game decal: its mesh, header and trailer are kept). The
// smaller levels are made by averaging, colours weighted by alpha.
std::vector<uint8_t> EncodeDecal(const std::vector<uint8_t>& rgba, int size, const std::vector<uint8_t>& templ);
// The RGBA image of a decal file (its size in `size`), or nullopt when it is not one.
std::optional<std::vector<uint8_t>> DecodeDecal(const std::vector<uint8_t>& file, int* size = nullptr);
// `rgba` (w x h) scaled to fit `size` x `size` in proportion, centred on a transparent square.
std::vector<uint8_t> FitImage(const std::vector<uint8_t>& rgba, int w, int h, int size);

// The keys of a string table (.str, "LSB2"), or nullopt when it is not one. A decal's DisplayName
// is "_&<table << 16 | key>"; the string lookup gets the 16-bit key.
std::optional<std::set<uint16_t>> StringHashes(const std::vector<uint8_t>& table);
inline constexpr uint32_t kDecalStringTable = 0xAF31;

// Decal ids: each image name keeps its id for good (a car's livery remembers its decals by id);
// a new name gets the lowest id never given out.
inline constexpr int kFirstDecalId = 9001;
inline constexpr int kMaxDecals = 999;
std::map<std::string, int> AssignIds(const std::vector<std::string>& names, std::map<std::string, int> ids);
std::map<std::string, int> ParseIds(std::string_view text);
std::string FormatIds(const std::map<std::string, int>& ids);

// One custom decal as the database has it.
struct LiveDecal {
  int id;
  std::string name;  // shown in the editor; also its place in the list (AlphaSort)
  std::string file;  // its file name in GAME:\Media\Livery\Decals
  uint16_t key;      // its name's string key
  // Its image has left the folder: the row and file stay, so a saved livery keeps it, but no picker lists it.
  bool retired = false;
};
// A name key for each id (in id order), none of `taken` (the game's Livery_Decals keys).
std::map<int, uint16_t> AssignKeys(const std::vector<int>& ids, const std::set<uint16_t>& taken);
// A decal file's name: its id and a hash of its image, so an edited image is a new file.
std::string DecalFileName(int id, uint64_t image_hash);
uint64_t ImageHash(const std::vector<uint8_t>& bytes);
// The statements that replace the custom rows of the database with `decals` (in no car's list:
// the custom items list them) and, with `all_sponsors`, every listless game decal after each car's
// usual ones.
std::vector<std::string> DecalStatements(const std::vector<LiveDecal>& decals, bool all_sponsors);

// The layer menus' custom decal items carry this action (one the menus do not use); as one is
// chosen it becomes the game's decal action (1), with the picker in custom mode until the next
// decal choice.
inline constexpr uint32_t kCustomDecalAction = 0x4D43;
inline constexpr uint32_t kGameDecalAction = 1;
void SetCustomPicker(bool custom);
bool CustomPicker();
// For a query of the decal picker's (its decals joined with a maker's list), the same columns of
// the custom decals alone, by name; nullopt for any other query.
std::optional<std::string> CustomPickerQuery(std::string_view sql);

// The state the hooks read, published by the decal builder.
struct Status {
  bool building = false;
  int decals = 0;
  std::vector<std::string> problems;  // each "<file> (<why>)", or that the folder is gone
  uint16_t apply_key = 0;             // the string keys of APPLY A / CHANGE CUSTOM DECAL (0: no items)
  uint16_t change_key = 0;
};
void Publish(std::vector<std::string> statements, std::map<uint16_t, std::u16string> names, Status status);
void SetBuilding();
Status CurrentStatus();
// The statements of the newest state when it is newer than `generation` (then updated); the
// game has taken that state (it takes it as the paint editor opens).
bool StatementsSince(uint64_t& generation, std::vector<std::string>* statements);
// Some of those statements failed: the next look takes them again, a few times, then the page says so.
void StatementsFailed(uint64_t& generation);
// True while decals are being made, or made but not yet taken by the game: the paint editor
// shows them the next time it opens.
bool ChangesWaiting();
// A custom decal's name for a string key, if it is one; also the menu items' labels, which say
// to reopen the editor while ChangesWaiting().
std::optional<std::u16string> DecalName(uint16_t key);
inline constexpr std::u16string_view kApplyLabel = u"APPLY A CUSTOM DECAL";
inline constexpr std::u16string_view kChangeLabel = u"CHANGE CUSTOM DECAL";
inline constexpr std::u16string_view kReopenNote = u" (REOPEN EDITOR TO UPDATE)";

// The package's folder name (under <user>\0000000000000000\4D530910\00000002) and offer number.
inline constexpr std::string_view kPackageName = "FM4CUSTOMDECALS00000000000000000000000000";
inline constexpr int kPackageOffer = 3999;

// At startup, before the game reads its content: keeps the package (the folder the decal files
// go in) and makes the decals of fm4_custom_decals_folder and fm4_all_sponsor_decals.
void InstallCustomDecals(const std::filesystem::path& game_data_root, const std::filesystem::path& user_root,
                         const std::filesystem::path& config_root);
// After the settings change: makes the decals again now rather than at the next look at the folder
// (every 2 seconds, in the background, as images are added, edited or taken out). The game takes
// them the next time the paint editor opens.
void ApplyCustomDecals();
// The settings the background builds read: the page sets them as it applies (the cvars are not safe across threads).
struct Settings {
  std::string folder;
  bool all_sponsors = false;
};
void SetSettings(Settings settings);
Settings CurrentSettings();
// Ends the background builds: before the runtime goes away.
void StopCustomDecals();
// What ApplyCustomDecals runs (InstallCustomDecals sets it); without one it does nothing.
void SetRebuild(std::function<void()> rebuild);

}  // namespace fm4_static_recomp::decals
