# Advanced Dirt Removal 2.0: user guide

Advanced Dirt Removal is one OFX effect. It finds dirt, dust and sparkle that
appear in a single frame and repairs them from the neighbouring frames. It also
includes a way to give back detail that should not have been repaired, the
recovery mask and auto recovery, and views for checking what changed.

- [Adding the effect](#adding-the-effect)
- [Input encoding](#input-encoding)
- [Cleaning controls](#cleaning-controls)
- [Recovery](#recovery)
- [Views](#views)
- [How the result is built](#how-the-result-is-built)
- [Diagnostics, speed and limits](#diagnostics-speed-and-limits)
- [Host notes](#host-notes)
- [Upgrading from 1.x](#upgrading-from-1x)

## Adding the effect

The effect is **Advanced Dirt Removal** in the **FabioColor** group. It has two
inputs: **Source** (the clip) and an optional **Mask**.

- **DaVinci Resolve, Fusion page:** Add Tool → OpenFX → FabioColor → Advanced
  Dirt Removal, then connect `MediaIn → Advanced Dirt Removal → MediaOut`.
  Connect a Mask Paint, Magic Mask or any mask tool to the effect's mask input.
- **Nuke:** Tab → "Advanced Dirt Removal". Input 1 is the source; connect a
  roto, paint or shape node to the Mask input.
- **Other OFX hosts:** look for the FabioColor group.

Give the effect the complete, unedited shot. It uses the two frames before and
the two after the frame being repaired, and it plays the shot through from its
first frame, so the first render of a shot takes longer than later ones.
The effect needs whole frames of 256 to 16,384 pixels per side. It works on the
RGB values as the host delivers them (float, half or 16-bit) and copies alpha
through unchanged.

## Input encoding

The first control. Pick what the values entering the effect are:

- **Video** (default): values as delivered, for video and footage already
  converted for viewing. The effect cleans them directly.
- **Cineon log:** Cineon-encoded film scans (black 95, white 685, film gamma
  0.6). The effect converts to linear, cleans in linear with an extended range,
  converts only its *change* back to log and adds it to the original log values.
  Pixels it does not repair keep their exact original values; the output stays
  in Cineon log.

## Cleaning controls

The published automatic-cleaning layout is retained: **Input encoding** and
**View**, followed by **Detection**, **Processing** and **Safeguards** in their
original order. All cleaning options stay in those groups. **Auto recovery**
follows them, then the integrated **Manual recovery / Spanish** section.
Start with the defaults. If real detail is being removed, lower **Sensitivity** or raise
**Motion guard**; if dirt remains, raise **Sensitivity** or try **Analyse: RGB**.

| Control | Default | What it does |
| --- | --- | --- |
| **Detection** | | |
| Analyse | Luma | Luma finds spots in brightness; RGB also finds spots that differ only in colour. |
| Remove dark spots | on | Repair spots darker than their surroundings (dust on a print, emulsion specks). |
| Remove light spots | on | Repair spots brighter than their surroundings (dirt on a negative, scratches through to the base). |
| Sensitivity | 0.75 | How readily a temporary change is treated as dirt (0 to 1). Higher finds fainter spots and risks repairing real detail. |
| Extra sensitivity | off | For heavily damaged material: drops detail preservation wherever enough neighbouring frames agree. Can remove real detail. |
| Analysis detail | 0.5 | Balance of fine against coarse analysis when separating dirt from grain and noise (0 to 1). |
| Limit spot size | off | Ignore spots larger than Largest spot. |
| Largest spot (px) | 25 | Largest spot diameter repaired when Limit spot size is on (1 to 256). |
| Extended-range input | off | Analyse values below 0 or above 1 (scene-referred material) instead of the normal display range. Cineon log turns this on automatically. |
| **Processing** | | |
| Repair amount | 1.0 | How strongly detected spots are replaced from neighbouring frames (0 to 2). |
| Blend repairs | on | Soften the edges of repairs so they blend with grain. |
| Blend amount | 0.5 | How much repair edges are softened (0 to 1). |
| **Safeguards** | | |
| Motion guard | 0.25 | Protect moving detail from being mistaken for dirt (0 to 1). Higher protects more and cleans less in motion. |
| Shot changes | Guard | Frames from another shot are never used for repair. Off: no extra care. Guard: repair frames next to a shot change more cautiously. Skip: leave frames next to a shot change unrepaired. |
| Shot-change guard | 0.75 | How cautious Guard is next to a shot change (0 to 1). |
| Flash guard | off | Leave frames detected as single-frame flashes unrepaired. |

Changing any cleaning control or the input encoding starts the analysis of the
shot again, and auto recovery calculates a new matte for the new settings.

## Recovery

Automatic repair sometimes removes things that belong to the picture: a
fast-moving hand, a spark, a highlight. Recovery gives the original back there.
In Resolve, **Method** selects Recovery or Spanish dirt removal; the collapsed
**Auto recovery** group holds the automatic matte controls. Other hosts keep
these controls in **Recovery**:

| Control | Default | What it does |
| --- | --- | --- |
| Method (Mode in other hosts) | Recovery | How painting and the Mask input are used: **Recovery** or **Spanish dirt removal** (below). |
| Calculate recovery | button | Calculates the auto-recovery matte for the whole shot now and turns Auto recovery on. |
| Auto recovery | Off | On: give back repaired spots the model judges to be real picture detail. |
| Confidence | 0.80 | Give back only spots the model is at least this sure about (0.50 to 0.99). Higher gives back less. |
| Matte folder | empty | Where calculated mattes are stored. Empty: the user's application data folder (below). |

### Integrated recovery

In Resolve, paint directly in this effect's viewer. No separate Mask Paint,
Magic Mask or recovery node is required. Automatic cleanup produces the working
image; the built-in strokes give original detail back in Recovery, or limit
cleanup to painted areas in Spanish dirt removal.

Older projects with an external Mask input still load and render that path when
the built-in brush is unused. Enabling Paint in viewer or keeping any built-in
strokes makes this effect's brush the sole manual recovery source for the clip.
The external mask is then ignored on every frame, including frames without a
stroke and while brush editing is off. This prevents overlapping corrections.

### Painting in Resolve

Development build only. Existing 2.0 installers do not include viewer painting.

The original automatic controls stay first. **Manual recovery / Spanish**
contains **Method**, Paint in viewer, Tool, Brush, Size, Softness, Opacity,
selection status and Undo/Redo. Its collapsed **Strokes**, **Brush options** and
**Source setup** sections contain the remaining manual controls. Collapse
Detection while concentrating on painting; available panel height and display
scaling determine how much scrolling is needed.

Painting is enabled by default. Choosing **Method: Recovery** or **Spanish dirt
removal** resumes Paint and clears the review selection. Turn on the viewer's
**Open FX Overlay**; **Paint in viewer** can pause editing when needed. On Color,
**Enhanced Viewer** (Shift-F) gives the picture more room. Choose **Paint**,
**Erase** or **Select**, then a **Round**, **Square** or **Rectangle** brush.
Resolve 21.1.1 disables Enhanced Viewer on Edit; use its standard viewer with
Open FX Overlay enabled there.
Drag a rectangle from corner to corner. **Recovery** gives the original back;
**Spanish dirt removal** repairs only where you paint.

Strokes stay off the picture: you see only the result and the brush. A stroke
you pick with **Select** is outlined; **Show strokes**, under **Brush options**,
outlines all of them. **Previous stroke** and **Next stroke** go through the
strokes in frame order and move the playhead to each one.
On Resolve Studio, stroke navigation requires **Preferences → System → General →
External scripting using: Local**. The plugin uses Resolve's bundled scripting
interpreter; no separate Python installation is needed.

**Select** lets you move a stroke or resize a rectangle by its corner handles.
The visible controls edit its size, softness and opacity; open **Strokes**
for duration, enable state, deletion and rectangle dimensions. **Delete stroke**
removes it; **Undo paint** brings it back, including its properties and selection.
**Redo paint** repeats the edit. Paint history is kept for the current editing
session and resets when the effect or project is reloaded. **Hold (frames)** defaults to one
frame. Held strokes stay in the same position; they do not track automatically.
**Tablet pressure**, under **Brush options**, varies the brush size with pressure.
**Smoothing** in the same group smooths the pen path while drawing.

Brush adjustments stay under the pointer: **Shift-drag** changes size and
**Option/Alt-drag** changes softness. Use the Tool and Brush selectors, and the
buttons under **Strokes**, for the other paint operations.

The plugin does not register or consume keyboard shortcuts. Resolve keeps its
frame navigation, bypass, undo/redo, text editing and other keyboard commands.
For the paint editor's separate history, use **Undo paint** and **Redo paint**.
Other hosts hide the viewer-paint controls. Physical tablet pressure still needs
an operator check.

### A processed export on V2, original on V1

Align the clips at the same native size and frame rate, without retiming,
transforms or colour matching. Copy `tools/prepare_recovery_source.py` into
Resolve's `Fusion/Scripts/Utility` folder, then run it from **Workspace →
Scripts → Utility**. Choose V2 as the working export and V1 as the recovery scan.
Preparation uses a separate timeline, verifies every source frame and restores
the working timeline and Deliver settings. It disables V1's ordinary output
only after verification; the clip and its media are preserved.

On V2 add Advanced Dirt Removal. Under **Source setup**, choose
**Prepared original**, select the verified **Original folder**, and leave
**Original frame offset** at 0. Use **Video** input encoding. Automatic recovery
pauses for this route. Painting blends the export and original without running
cleanup.
Keep that source folder with the project. Missing, corrupt or mismatched source
frames stop rendering with an error.

### Auto recovery

A small neural network, trained on restorers' decisions about which repaired
spots were real picture detail, looks at every spot the effect repaired and gives
it a probability. Spots at or above **Confidence** are given back.

- **Calculate recovery** works through the whole shot and saves one probability
  map per frame. The button returns immediately; the calculation runs with the
  next render (on an Apple M5 Pro, about 1.5 to 3 seconds per 2.4K frame). Without
  the button, any frame that has no map yet is calculated when it renders.
  These files store recovery mattes. Use Resolve's render cache or the prepared
  source workflow to retain cleaned pictures.
- The development candidate adds a **Calculation** status showing queued work,
  frame counts, completion time, cancellation or failure. It requests the host's
  progress display when available. While a whole-shot calculation is queued or
  running, the button becomes **Cancel calculation**. Valid saved mattes are
  reused; a completely cached shot needs no new cleaning or classification.
  The status refreshes when the host updates the viewer or controls. Native
  Resolve progress-display behavior still needs verification for this candidate.
- Maps are stored per shot, keyed by the shot's first frame, its frame range and
  size, the input encoding and every cleaning control. Change any of these and the
  matte is calculated again automatically; changing back reuses the earlier maps.
  Changing **Confidence** never recalculates: it only moves the threshold.
- Default matte folders:
  - macOS: `~/Library/Application Support/FabioColor/AdvancedDirtRemoval/Recovery`
  - Windows: `%LOCALAPPDATA%\FabioColor\AdvancedDirtRemoval\Recovery`
  - Linux: `$XDG_DATA_HOME/fabiocolor/advanced-dirt-removal/recovery` (or
    `~/.local/share/...`)

  Each shot is a `shot-<id>` folder of small run-length-encoded files. Delete the
  folders to free space; they are recalculated when needed.

**Editing the automatic matte.** Built-in paint gives original detail back;
Erase keeps the cleaned result where the model recovered too much. Untouched
areas continue to follow the automatic matte. There is one finishing pass.
Legacy external masks keep their white/black behavior only when the built-in
brush is unused. **Auto recovery matte** displays the model's matte.

### Spanish dirt removal (Method: Spanish dirt removal)

Only painted areas are repaired; everything else keeps the original. With
nothing painted, Spanish leaves the whole picture unrepaired. In Resolve,
automatic recovery pauses in Spanish mode, preserving its saved setting for
when Recovery is selected again. It also pauses for prepared processed exports,
which do not run the cleaner or its model. Other OFX hosts retain the published
external-mask behavior and require Auto recovery off for Spanish mode.

The development candidate bypasses cleaning on Resolve frames with no active
built-in strokes. On a painted frame, the cleaner still calculates a full-frame
temporal result and applies it only inside the brush. That result is cached for
subsequent brush edits. This is not yet computation restricted to the brush
area: the first painted-frame request, a cold seek, changed cleaning settings or
a host cache purge can still require motion analysis. A Spanish node after an
active automatic-cleaning node also pays for that upstream node's processing.
Rejected proxy/thumbnail requests no longer discard a valid full-resolution
cleaning cache; proxy rendering itself remains unsupported.

## Views

**View** sits next to Input encoding. It changes only what you see; the
repair is the same.

| View | Shows |
| --- | --- |
| Restored | the result (default) |
| Red difference | a grey picture with every changed pixel in red |
| True difference | the absolute change, multiplied by **Difference gain** |
| Auto recovery matte | white where auto recovery gives the original back |

**View options:**

| Control | Default | What it does |
| --- | --- | --- |
| Difference threshold (%) | 0.1 | Changes smaller than this are not shown in red. The effect moves pixels it does not repair by less than one 10-bit code value, so 0.1% shows only real repairs; 0 shows every change. |
| Red difference intensity | 8 | Brightness of the red highlights: largest channel change × intensity. |
| Red highlight opacity | 0.85 | Opacity of the red highlights. |
| Grayscale background | 1 | Brightness of the grey picture behind the highlights (0 = black). |
| Difference gain | 4 | Gain of the True difference view. |

Red difference, per pixel: `d` = largest change of R, G or B; red =
`min(d × intensity, 1)` where `d` is above the threshold, else 0; opacity = red ×
highlight opacity; grey = Rec. 709 luma of the source × background; output =
grey × (1 − opacity) plus opacity in the red channel.

## How the result is built

```
repair  = cleaned frame (Video) or source + log(clean(linear)) − log(linear) (Cineon log)
gate    = Recovery, auto off:      1 − mask
          Recovery, auto on:       1 − (matte × (1 − mask) + stroke colour × mask)
          Spanish dirt removal:    mask
result  = source where gate = 0, repair where gate = 1, source + (repair − source) × gate between
```

With the default settings (Video, Restored, Auto recovery off, no recovery
strokes or legacy mask) the output is exactly the cleaned frame. The formulas
above describe the compatibility mask; built-in paint/erase overrides the
automatic matte by its stroke coverage, without also applying that external mask.

## Diagnostics, speed and limits

- **Same result everywhere.** Every GPU backend (Metal, CUDA, OpenCL) and the CPU
  fallback give byte-identical results, so a render on one machine matches a
  render on any other. Auto-recovery maps are also identical between hosts.
- **Speed.** On an Apple M5 Pro (Metal) a 2368×1712 frame cleans in about 0.1 s.
  The CPU fallback works without a GPU but is much slower.
- **Backend.** The effect tries Metal, CUDA, OpenCL, then the CPU, and uses the
  first that works. CUDA needs an NVIDIA GPU with compute capability 7.5
  (Turing) or newer; older NVIDIA GPUs, such as the Quadro P5000, use OpenCL
  automatically. Set `ADR_BACKEND=metal|cuda|opencl|cpu` (optionally
  `opencl:1` for a device) to force one.
- **Log.** The effect keeps a diagnostic log (backend, device, timing, errors):
  `~/Library/Logs/FabioColor/AdvancedDirtRemoval` (macOS),
  `%LOCALAPPDATA%\FabioColor\AdvancedDirtRemoval\Logs` (Windows),
  `~/.local/state/fabiocolor/advanced-dirt-removal/logs` (Linux). Attach it to
  issue reports. `ADR_LOG=off` turns it off.
- **Review moving shots.** Very fast motion can look like dirt. Raise Motion
  guard, use auto recovery, and paint back what is left.
- **Interlaced material** is not supported; deinterlace first.

## Host notes

- **DaVinci Resolve, Fusion page:** tested with Resolve Studio 21 on macOS,
  Windows and Linux. Fusion shows all controls on one page; it does not use
  the effect's pages. A mask made at another resolution covers the same
  picture (Fusion is resolution-independent).
- **DaVinci Resolve, Color page:** not tested yet. The plan is for the effect to
  work there too.
- **Nuke:** tested with Nuke 17 on macOS, Windows and Linux. The mask is read at the same pixel position as the source
  (Nuke's usual behaviour); a mask with a smaller data window is zero outside it.
- **Other OFX hosts:** not tested; please report results.
- **Adobe Premiere Pro and After Effects** do not load OFX plugins. An adapter
  is planned.

## Upgrading from 1.x

Version 1.x was a Fusion macro for DaVinci Resolve that wrapped Resolve's
built-in dirt removal. Version 2.0 is a stand-alone effect with its own repair
engine that works in any OFX host, and everything the macro did is now built in:
the encoding choice, the recovery brush, Spanish dirt removal and the red
difference view. The optical-flow motion mask of 1.x is replaced by **Motion
guard** and auto recovery. On macOS and Linux, installing 2.0 leaves a 1.x
macro in place; on Windows the installer replaces the 1.x installation
([INSTALL.md](INSTALL.md)).

### Stroke navigation in Color and Edit

Previous/Next stroke, or `,` and `.`, visits strokes in frame order and moves
the playhead to each stroke's first frame, including held strokes. Color/Edit
navigation uses Resolve's bundled Lua interpreter and local scripting API when
the OFX timeline suite cannot seek. There is no Python dependency or utility to
launch. Resolve Studio must allow Local external scripting. Mapping follows the
current clip and trims; navigation does not change stroke data or image pixels.
**Show strokes** defaults off. Painting shows the result and brush cursor; an
explicit Select action shows only the selected shape. Turn Show strokes on for
review when outlines are useful.
