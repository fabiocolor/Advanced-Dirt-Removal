# Documentation media

Sources for every file: [SOURCES.md](SOURCES.md).

| File | Shows |
| --- | --- |
| `cleanup.mp4` / `.gif` | Split view, full frame: source on the left, restored on the right |
| `difference.mp4` / `.gif` | The Red difference view, full frame |
| `mask.mp4` / `.gif` | The Mask input with one painted ellipse, side by side: Recovery and Spanish dirt removal |
| `auto-recovery.mp4` / `.gif` | The Red difference view, side by side: auto recovery off and on |
| `macos-resolve-2.0-restored.png` | The plugin in Resolve Studio 21.1 on macOS (Fusion page), Restored view |
| `macos-resolve-2.0-red-difference.png` | The same, View set to Red difference |
| `resolve-2.0-controls.png`, `resolve-2.0-red-difference.png` | Earlier captures with a synthetic clip |

## How they were made

Rendered on an Apple M5 Pro (Metal) with the engine's command-line tool,
`adr_bench`, at the default settings, from the full 2048 × 1556 scans. That is
the same engine the plugin runs, with the same output. The views use the
formulas in the [user guide](../../docs/USER_GUIDE.md#how-the-result-is-built)
with the default view options. Auto recovery uses the engine's maps for the
shot at the default Confidence (0.80).

The MP4s are full quality at 24 fps (H.264): 1920 wide for one view, 2560 wide
for the side-by-side clips. The GIFs show the whole frame at 640–960 wide,
12 fps, 8–9 s, with a light spatial grain filter to keep them small. Nothing
was retouched.

The screenshots are from macOS. Resolve and the plugin's controls look the same
on Windows and Linux. They are window-only captures of Resolve Studio
21.1.1.10 with the installed 2.0.0 package, using a throwaway project in the
local database.

The 1.x images in `assets/images` are kept for history.
