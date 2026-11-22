# Track signage

[← Back to the README](../README.md)

Put your own pictures on the track's boards: banners, barrier boards, logos painted on the run-off,
flags and signs. Choose a picture for each board in PC OPTIONS and it shows on track at once, even
mid-race. The game's files are not changed.

## The SIGNAGE page

In PC OPTIONS (**F6**), the **SIGNAGE** tab:

| Row | What it does |
|---|---|
| **TRACK SIGNAGE** | ON (default) shows your pictures; OFF shows the default boards everywhere and keeps your choices for later. |
| **HOCKENHEIMRING** | Opens the track's boards (below). Its note says how many boards have a picture of yours. |
| **SIGNAGE FOLDER** | OPEN (A or Enter) opens the folder your pictures come from. Its note says how many pictures are in it, and where it is. |

On a track's boards:

- **LEFT / RIGHT** choose the picture a board shows: **DEFAULT**, or any PNG in the signage folder.
- The description shows **the default board** and **yours** side by side, the board's size, and the size
  your picture is shown at.
- **RESET ALL BOARDS** puts every board of the track back to its default.
- **B** goes back to the tracks; **SAVE & APPLY** (Y) shows the choices on track straight away.

## Pictures

- Any PNG in the signage folder, named as you like. Every board can use any picture, and one picture
  can go on several boards.
- The picture is stretched to the board's shape. It looks best at the board's size or at 2× or 4×
  it: a bigger picture gives a sharper board (up to 4×).
- A picture you change in the folder changes on track within a second or two.
- Transparency is kept: the board uses it as it uses its own.

The folder sits beside the game's `config` folder (a launcher install: `data\signage`; a manual
install: `.local\state\signage`). The choices are saved in `config\signage.toml`.

## Hockenheimring

Named by what each board is and where it is on the circuit. Many are painted on the run-off or are
barriers and flags. The first two are the circuit's surfaces: one picture each, tiled over the whole
track, so a seamless texture (one that repeats without visible edges) looks best.

| Board | Size |
|---|---|
| ROAD SURFACE | 512 × 512 |
| GRASS | 512 × 512 |
| SANTANDER WALL, START STRAIGHT | 1024 × 128 |
| SANTANDER SIGN, PIT BUILDING | 512 × 64 |
| SANTANDER LOGO ON THE RUN-OFF | 1024 × 256 |
| SANTANDER BANNER, START GANTRY | 256 × 256 |
| ALLIANZ BARRIER BOARDS | 1024 × 256 |
| ALLIANZ LOGO ON THE TRACK | 1024 × 256 |
| ALLIANZ BOARD, GREY (not seen on the track yet) | 1024 × 256 |
| MOBIL 1 BOARD (not seen on the track yet) | 1024 × 256 |
| MOBIL 1 BILLBOARD, TRACKSIDE | 1024 × 256 |
| MOBIL 1 LOGO ON THE RUN-OFF | 1024 × 512 |
| KAERCHER / ENI, GRANDSTAND | 1024 × 256 |
| HOCKENHEIMRING LOGO BOARDS | 1024 × 128 |
| HOCKENHEIMRING NAME WALL | 512 × 64 |
| HOCKENHEIMRING LOGO, PIT ROOF | 256 × 64 |
| MORE THAN RACING, STAND ROOF | 1024 × 256 |
| HOCKENHEIMRING TOWER SIGN | 256 × 128 |
| SACHS KURVE BARRIER | 1024 × 128 |
| DUNLOP FLAGS | 128 × 256 |
| KIOSK SIGN, INFIELD | 128 × 64 |
| PANASONIC LOGO ON THE RUN-OFF | 1024 × 256 |
| AGIP BOARD (not seen on the track yet) | 512 × 256 |
| MERCEDES-BENZ, STAND ROOF | 1024 × 128 |
| BRIDGESTONE, ON THE RUN-OFF | 1024 × 256 |
| RICO ANTHES, ON THE ROAD | 512 × 128 |
| BETONSCHUTZWAND BARRIER | 1024 × 128 |
| HOTEL TERRASSE, GRANDSTAND | 1024 × 128 |
| BADEN-WUERTTEMBERG TOWER | 1024 × 128 |
| SPONSOR BANNERS, STAND ROOF | 1024 × 512 |
| ADAC BANNERS ON THE FENCE | 256 × 128 |
| INFO BOARD ON A POLE | 128 × 64 |
| AUSFAHRT SIGN, PIT EXIT | 256 × 64 |

## Sharper track textures

The SIGNAGE page's **TEXTURES** rows make the track's own textures sharper, Hockenheimring for now:

| Row | What it does |
|---|---|
| **TRACK TEXTURES** | DEFAULT, 2X or 4X: the textures worth it (373 of Hockenheimring's 783: not normal maps, flat colours or masks). |
| **ROAD SURFACE** | The road alone, over TRACK TEXTURES. |
| **GRASS** | The grass alone, over TRACK TEXTURES. |

- The first time a size is chosen, the game upscales the textures from **your own disc**, in the background. The
  row's note counts them ("UPSCALING 2X: 120 OF 373"), and they show as soon as they're done. On the test PC the
  whole track took under four minutes. After that a size shows at once.
- It uses [Real-ESRGAN](https://github.com/xinntao/Real-ESRGAN) (BSD 3-Clause), downloaded once (45 MB) at a pinned
  version and checked against its SHA-256 before use, into `tools\realesrgan` beside the config folder. Each
  texture is upscaled 4x with a border of its own wrapped edges, so it stays seamless when tiled, and keeps the
  game's colours and alpha (cut-out fences and leaves stay cut out). 2X is the 4X result halved.
- The results are kept in `track-textures` beside the config folder, the settings in `config\track-textures.toml`.
  Delete the folder to make them again. They come from your disc: nothing of the game's is downloaded or shared.
- A picture of your own on the road or grass (above) is shown instead of the sharper texture.
- 4X takes more video memory: about four times 2X's.

## Turning it off

**TRACK SIGNAGE: OFF** on the page, or `--fm4_track_signage=false` (a
[launch option](launch-options.md)), shows every board's default.

## How it works

Each board is one of the track's textures, found by the hash the game's texture replacement uses.
The recomp converts your picture to that texture's format (DXT5) at the chosen size, with every
smaller level, and hands it to the renderer as a run-time replacement.
