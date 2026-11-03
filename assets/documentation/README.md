# Documentation media

## Sources

| Footage | Used for | Source |
| --- | --- | --- |
| Trailer for *The Evictors* (1979), shown with *Island of Death* | Before/after wipe, Mask, the 1:1 detail still | 35mm trailer scan, 2048 × 1556, 10-bit ProRes, from Fabio Bedoya's film trailer collection |
| Trailer for *Breathless* (1983), Richard Gere | Red difference GIF and still, both videos, the Resolve screenshots | 35mm trailer scan, 2048 × 1556, 10-bit ProRes, from the same collection |
| *Neighbors* (1920), Buster Keaton and Edward F. Cline | Auto recovery | Wikimedia Commons, public domain: https://commons.wikimedia.org/wiki/File:Neighbors_(1920)_by_Buster_Keaton_and_Edward_F._Cline.webm |

The *Neighbors* file is a compressed web copy at 3840 × 2160, scaled to
2048 × 1152 here. The trailer scans are used at their own size for the GIFs
(crops shown at 960 wide, 1:1); the videos are scaled to 1024 wide.

## Files

| File | What it shows |
| --- | --- |
| `adr-2.0-before-after.gif` | A wipe between source and restored on the trailer's title card, default settings |
| `adr-2.0-red-difference.gif` | A close-up from the *Breathless* trailer in the Red difference view |
| `adr-2.0-mask.gif` | The Mask input on the title card, one painted ellipse: Recovery, then Spanish dirt removal |
| `adr-2.0-auto-recovery.gif` | *Neighbors*, Red difference view, auto recovery off and then on |
| `adr-2.0-before-after.mp4` | 26 s of the *Breathless* trailer with a wipe between source and restored |
| `adr-2.0-red-difference.mp4` | The same 26 s in the Red difference view |
| `adr-2.0-detail-before-after.png` | 1:1 detail of the title card, source and restored |
| `adr-2.0-red-difference.png` | One frame of the *Breathless* close-up in the Red difference view |
| `macos-resolve-2.0-restored.png` | The plugin in Resolve Studio 21.1 on macOS (Fusion page), Restored view |
| `macos-resolve-2.0-red-difference.png` | The same, View set to Red difference |
| `resolve-2.0-controls.png` | Earlier capture: the controls in Resolve on macOS (synthetic clip) |
| `resolve-2.0-red-difference.png` | Red difference and its view options in Resolve (synthetic clip) |

## How they were made

Rendered on an Apple M5 Pro (Metal) with the engine's command-line tool,
`adr_bench`, at the default settings. That is the same engine the plugin runs,
with the same output. The views were drawn with the formulas in the
[user guide](../../docs/USER_GUIDE.md#how-the-result-is-built), at the default
view options:

- Red difference: every changed pixel in red on a grey picture.
- Mask, Recovery: inside the painted area the original is kept.
- Mask, Spanish dirt removal: only the painted area is repaired.
- Auto recovery on: the engine's auto-recovery maps for the shot, at the
  default Confidence (0.80), give the original back where the matte is set.

The GIFs are crops at 10 frames per second: the trailer GIFs at 960 wide
(1:1), the auto-recovery GIF at 640 wide. Nothing was retouched.

## Screenshots

The screenshots are from macOS only. Resolve and the plugin's controls look
the same on Windows and Linux. The `macos-resolve-2.0-*` screenshots are
unedited captures of Resolve's window only (scaled to 1920 wide), taken on
3 November 2026 with the installed 2.0.0 package in Resolve Studio 21.1.1.10.
They use a throwaway test project in the local database, deleted afterwards.
Their clip is 48 frames of the *Breathless* close-up.

The `resolve-2.0-*` screenshots show the engine's synthetic test clip (seed 11,
512 × 512, frame 5): the interface, not picture quality.

No machine paths or credentials appear in these files. The 1.x images in
`assets/images` are kept for history.
