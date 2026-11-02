# Advanced Dirt Removal 2.0: user guide

Advanced Dirt Removal is one OFX effect. It looks for dirt, dust and sparkle
that appear in a single frame and fills them from the neighbouring frames. So it
masks damage with picture from nearby frames; it doesn't invent detail. It also
has ways to give back detail that shouldn't have been repaired (the recovery
mask and auto recovery), and views to check what changed.

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
the two after the frame being repaired. It also plays the shot through from its
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

On the **Cleaning** page (in hosts that show pages), in three groups. Start with
the defaults. If real detail is being removed, lower **Sensitivity** or raise
**Motion guard**. If dirt remains, raise **Sensitivity** or try **Analyse: RGB**.

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
fast-moving hand, a spark, a highlight. Recovery puts the original back there.
The **Recovery** group holds:

| Control | Default | What it does |
| --- | --- | --- |
| Mask | Recovery | How the Mask input is used: **Recovery** or **Spanish dirt removal** (below). |
| Calculate recovery | button | Calculates the auto-recovery matte for the whole shot now and turns Auto recovery on. |
| Auto recovery | Off | On: give back repaired spots the model judges to be real picture detail. |
| Confidence | 0.80 | Give back only spots the model is at least this sure about (0.50 to 0.99). Higher gives back less. |
| Matte folder | empty | Where calculated mattes are stored. Empty: the user's application data folder (below). |

### The recovery mask (Mask: Recovery)

Paint on the Mask input where the original should be kept. Painted areas show
the source; everything else shows the repair. Soft edges and partial opacity
blend between the two. The mask is the input's alpha (or its first channel if it
has no alpha), so a Mask Paint, Magic Mask, roto shape or plain alpha all work.

### Auto recovery

A small neural network looks at every spot the effect repaired and gives it a
probability of being real picture detail. Spots at or above **Confidence** are
given back. It gets a lot right, but only up to a point: some real detail stays
repaired and some dirt comes back. Check the matte and paint over the mistakes.
The model's weights are under CC BY-NC 4.0, not the MIT licence of the code
([WEIGHTS_LICENSE](../WEIGHTS_LICENSE)).

- **Calculate recovery** works through the whole shot and saves one probability
  map per frame. The button returns immediately; the calculation runs with the
  next render (on an Apple M5 Pro, about 1.5 to 3 seconds per 2.4K frame). Without
  the button, any frame that has no map yet is calculated when it renders.
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

**Editing the matte with the mask.** With Auto recovery on and Mask set to
Recovery, the Mask input edits the matte: unpainted areas follow the matte,
**white** strokes give the original back, **black** strokes keep the repair (take
back something the model gave back). Use **View: Auto recovery matte** to see the
matte.

### Spanish dirt removal (Mask: Spanish dirt removal)

The opposite use of the mask: only painted areas are repaired, and everything
else keeps the original. Useful for treating one damaged area of a shot. Spanish
dirt removal does not use auto recovery: with Auto recovery on, the render stops
with the error *"Auto recovery cannot be used with Spanish dirt removal"*. Turn
one of them off and it renders again. With nothing connected to the Mask input,
Spanish dirt removal leaves the whole picture unrepaired.

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

With the default settings (Video, Restored, Auto recovery off, nothing on the
Mask input) the output is exactly the cleaned frame.

## Diagnostics, speed and limits

- **Same result everywhere.** Every GPU backend (Metal, CUDA, OpenCL) and the CPU
  fallback give identical output, so a render on one machine matches a render
  on another. Auto-recovery maps are identical between hosts too.
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
engine, for OFX hosts. Everything the macro did is built in now: the encoding
choice, the recovery brush, Spanish dirt removal and the red difference view. The optical-flow motion mask of 1.x is replaced by **Motion
guard** and auto recovery. On macOS and Linux, installing 2.0 leaves a 1.x
macro in place; on Windows the installer replaces the 1.x installation
([INSTALL.md](INSTALL.md)).
