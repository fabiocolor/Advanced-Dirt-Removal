# Documentation media

## Footage

The GIFs, videos and `adr-2.0-*` stills use one shot from *JOIN THE NAVY: 8
Classic US Navy Recruitment Ads* (1966–1977), a 16mm colour scan. The ads were
made by the US Navy, so they are in the public domain in the United States.
The scan is from Wikimedia Commons, marked `PD-USN`:
https://commons.wikimedia.org/wiki/File:JOIN_THE_NAVY-_8_Classic_US_Navy_Recruitment_Ads_-_16mm_Film_Scan.webm

The shot is the portrait of Master Chief Carl Brashear in *Profile: Master
Diver Carl Brashear* (1976): 6 seconds from 371.0 s (144 frames at 24 fps),
scaled to 2048 × 1536 and cropped to 1320 × 990 at (420, 380). The crop leaves
out the scanner's watermark in the corner.

The Commons file is a compressed web copy of the scan, not the scan itself.
It still has plenty of dust and specks to show what the effect does.

## Files

| File | What it shows |
| --- | --- |
| `adr-2.0-before-after.gif` | A wipe between source and restored, default settings (640 wide, 20 frames at 10 fps) |
| `adr-2.0-red-difference.gif` | The same frames in the Red difference view |
| `adr-2.0-mask.gif` | The Mask input with one painted ellipse: Recovery, then Spanish dirt removal |
| `adr-2.0-before-after.mp4` | The whole shot with a slow wipe between source and restored |
| `adr-2.0-red-difference.mp4` | The whole shot in the Red difference view |
| `adr-2.0-detail-before-after.png` | 1:1 detail of frame 24, source and restored |
| `adr-2.0-red-difference.png` | Frame 24 in the Red difference view |
| `macos-resolve-2.0-restored.png` | The plugin in Resolve Studio 21.1 on macOS (Fusion page), Restored view (earlier test clip, a 1943 public-domain US Navy film) |
| `macos-resolve-2.0-red-difference.png` | The same, View set to Red difference |
| `resolve-2.0-controls.png` | Earlier capture: the controls in Resolve on macOS (synthetic clip) |
| `resolve-2.0-red-difference.png` | Red difference and its view options in Resolve (synthetic clip) |

## How they were made

Rendered on an Apple M5 Pro (Metal) with the engine's command-line tool,
`adr_bench`, at the default settings. That is the same engine the plugin runs,
with the same output. The views and the mask were drawn with the formulas in
the [user guide](../../docs/USER_GUIDE.md#how-the-result-is-built), at the
default view options:

- Red difference: every changed pixel in red on a grey picture.
- Mask, Recovery: inside the painted area the original is kept.
- Mask, Spanish dirt removal: only the painted area is repaired.

The painted area is a soft ellipse over the wall, around a large speck that is
there for one frame only. Nothing was retouched.

Auto recovery isn't animated. On this shot it gives back some spots that are
clearly dirt (including that large speck), so a GIF of it would not be fair to
show as the feature working. Use it as a help and check its matte.

## Screenshots

The screenshots are from macOS only. Resolve and the plugin's controls look
the same on Windows and Linux. The `macos-resolve-2.0-*` screenshots are
unedited captures of Resolve's window only (scaled to 1920 wide), taken on
2 November 2026 with the installed 2.0.0 package in Resolve Studio 21.1.1.10.
They use a throwaway test project in the local database, deleted afterwards.
Their clip is an earlier one: *The History of Naval Aviation* (1943), also a
public-domain US Navy film (`PD-USGov-Navy` on Wikimedia Commons).

The `resolve-2.0-*` screenshots show the engine's synthetic test clip (seed 11,
512 × 512, frame 5): the interface, not picture quality.

No private footage, machine paths or credentials appear in these files. The
1.x images in `assets/images` are kept for history.
