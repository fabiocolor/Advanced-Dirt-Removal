# Documentation media

## Footage

The videos and stills use a short excerpt of *The History of Naval Aviation*
(1943), a US Navy film and so in the public domain in the United States. The
16mm scan is by The Scan Man Gallery, from Wikimedia Commons, marked
`PD-USGov-Navy`:
https://commons.wikimedia.org/wiki/File:The_History_of_Naval_Aviation_(1943)_-_16mm_-_Flat_-_Mono_-_UHD.webm

The Commons file is a compressed web copy, not a scan master, so it is softer
than a real scan and some compression noise is mixed in with the grain. It
still has plenty of dust and specks to show what the effect does.

Excerpt: 5 seconds from 924.5 s (120 frames at 24 fps), cropped to the
picture area (2816 × 2160 at x = 512) and scaled to 2048 × 1570.

## Files

| File | What it shows |
| --- | --- |
| `adr-2.0-before-after.mp4` | Source on the left, restored on the right, default settings |
| `adr-2.0-red-difference.mp4` | The same shot in the Red difference view |
| `adr-2.0-detail-before-after.png` | 1:1 detail of one frame, source and restored |
| `adr-2.0-red-difference.png` | One frame in the Red difference view |
| `resolve-2.0-controls.png` | The plugin's controls in Resolve Studio 21.1 on macOS (synthetic clip) |
| `resolve-2.0-red-difference.png` | Red difference and its view options in Resolve (synthetic clip) |

The videos and the `adr-2.0-*` stills were rendered on an Apple M5 Pro (Metal)
with the engine's command-line tool, `adr_bench`, at the default settings.
That is the same engine the plugin runs, with the same output. Red difference
was drawn with the formula in the [user guide](../../docs/USER_GUIDE.md#views),
at the default view options. Nothing was retouched. You can see what the
effect leaves alone: the long vertical scratches stay, because they are there
for many frames, not one.

The two `resolve-2.0-*` screenshots are unedited captures of Resolve Studio
21.1.1.10 on macOS with the installed 2.0.0 package. Their viewer shows the
engine's synthetic test clip (seed 11, 512 × 512, frame 5), so they show the
interface, not picture quality.

No private footage, machine paths or credentials appear in these files. The
1.x images in `assets/images` are kept for history.
