# Track signage

[← Back to the README](../README.md)

Put your own pictures on the trackside boards: a PNG named after a board goes in the **signage**
folder, and races show it on that board. It changes within a few seconds, even mid-race; delete
the file and the game's board comes back. There is no setting: a file in the folder is what turns
its board on.

## Where

The `signage` folder sits beside the game's `config` folder:

- a launcher install: `data\signage`
- a manual install: `.local\state\signage` (or `signage` in the folder you passed as `-StateRoot`)

The game makes the folder at startup, with a `README.txt` that lists every board.

## The boards

The first boards are Hockenheim's. Name the file exactly as below (capitals don't matter):

| File | Size | Board |
|---|---|---|
| `hockenheim-santander-wall.png` | 1024 × 128 | The red wall along the start straight |
| `hockenheim-mobil1.png` | 1024 × 256 | The Mobil 1 boards |
| `hockenheim-allianz-grey.png` | 1024 × 256 | The grey Allianz boards |
| `hockenheim-allianz-blue.png` | 1024 × 256 | The blue Allianz boards |
| `hockenheim-hotel.png` | 1024 × 128 | The HOTEL RESTAURANT TERRASSE sign |
| `hockenheim-ring-logo.png` | 256 × 64 | The HOCKENHEIMRING logo boards |
| `hockenheim-ring-name.png` | 512 × 64 | The hockenheimring name boards |
| `hockenheim-bw-center.png` | 1024 × 128 | The Baden-Württemberg Center banner |
| `hockenheim-sponsors.png` | 1024 × 512 | The sponsor banners (four strips, top to bottom) |

- The picture is stretched to the board's shape. It looks best at the size given, or at 2× or 4×
  it: a bigger picture gives a sharper board (up to 4×).
- Its transparency is used as the board uses its own. On the Santander wall the alpha is where
  the wall shines; elsewhere a picture without transparency is the safe choice.
- A board is matched by its texture, so a track that uses the same texture shows the picture too.

## Turning it off

`--fm4_track_signage=false` (a [launch option](launch-options.md)) leaves every board as the
game's, whatever is in the folder.

## How it works

Each board is one of the track's textures, found by the hash the game's texture replacement
uses. The recomp converts the picture to that texture's format (DXT5) at the chosen size, with
every smaller level, and hands it to the renderer as a run-time replacement; the game's files
are not changed.
