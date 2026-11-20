# Advanced Dirt Removal 2.1: user guide

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
- [Diagnostics, speed and limits](#diagnostics-speed-and-limits)
- [Host notes](#host-notes)
- [Upgrading from 1.x](#upgrading-from-1x)

## Adding the effect

The effect is **Advanced Dirt Removal** in the **FabioColor** group. It has
three inputs: **Source** (the clip), an optional **Mask** and an optional
**Additional source** ([below](#a-processed-export-and-its-original)).

- **DaVinci Resolve:** on the Fusion page, Add Tool → OpenFX → FabioColor →
  Advanced Dirt Removal, then connect `MediaIn → Advanced Dirt Removal →
  MediaOut`. On the Color and Edit pages, drag it from the OpenFX library onto
  a node or clip. You can paint directly in the effect's viewer
  ([Painting in Resolve](#painting-in-resolve)), or connect a Mask Paint,
  Magic Mask or any mask tool to the mask input.
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

In three groups: on the **Cleaning** page in hosts that show pages, and under
**Automatic cleaning** in Resolve. Start with the defaults. If real detail is being removed, lower **Sensitivity** or raise
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
In Resolve, **Method** is in the **Paint** group and the other controls are in
**Automatic cleaning → Auto recovery**. Other hosts keep them all in
**Recovery**:

| Control | Default | What it does |
| --- | --- | --- |
| Method (Mode in other hosts) | Recovery | How painting and the Mask input are used: **Recovery** or **Spanish dirt removal** (below). |
| Calculate recovery | button | Calculates the auto-recovery matte for the whole shot now and turns Auto recovery on. |
| Auto recovery | Off | On: give back repaired spots the model judges to be real picture detail. |
| Confidence | 0.80 | Give back only spots the model is at least this sure about (0.50 to 0.99). Higher gives back less. |
| Matte folder | empty | Where calculated mattes are stored. Empty: the user's application data folder (below). |

### Painting in Resolve

In DaVinci Resolve you paint directly in the effect's viewer, on the Fusion,
Color and Edit pages. No Mask Paint node is needed. Turn on the viewer's
**Open FX Overlay**, pick the **Method** and paint. In **Recovery** the strokes
give the original back; in **Spanish dirt removal** only the painted spots are
repaired.

- **Tool:** Paint, Erase (take paint away) or Select (click a stroke to move or
  resize it). **Brush:** Round, Square or Rectangle (drag from corner to
  corner).
- **Shift-drag** in the viewer changes the size, **Option/Alt-drag** the
  softness. The plugin uses no keyboard shortcuts; the keyboard stays with
  Resolve.
- **Show strokes** outlines the strokes on the current frame and switches to
  Select. **Strokes** holds **Previous stroke** and **Next stroke**, **Delete
  stroke**, **Hold (frames)** and the rectangle size. A held stroke stays in
  place; it doesn't follow motion. On Color and Edit, Previous/Next stroke
  needs Preferences → System → General → External scripting using: **Local**.
- **Undo paint** and **Redo paint** undo stroke edits without touching Resolve's
  own undo. The history lasts until the project is closed.
- **Brush options:** Opacity, Smoothing and Tablet pressure.
- **Paint in viewer** pauses editing. While the brush is used, it replaces the
  Mask input. To use a mask connected to the Mask input instead, turn off
  **Paint in viewer** and delete the strokes.

Recovery and Spanish dirt removal each keep their own cleaning settings in the
project. Spanish starts with stronger settings (Sensitivity 1.0, Extra
sensitivity on, Analysis detail 1.0, Repair amount 1.5, Motion guard 0) and
you can change them. Switching **Method** brings back that method's settings.

**Cleaning defaults** (under Automatic cleaning) saves your own settings for
the selected method. **Load my default** loads them in another node or project;
**Use factory settings** goes back to the defaults above. Nothing is loaded
automatically.

### A processed export and its original

To paint a clip that was already cleaned elsewhere back to the original, put
the processed export and the original in line, at the same size and frame rate.
Connect the original to the effect's **Additional source** input, choose
**Source setup → Paint source: Additional source** and leave Input encoding on
Video. Painting in Recovery gives the original back. Nothing is cleaned and
auto recovery is paused; only the current frame is read.

### The recovery mask (Mask input)

In hosts without viewer painting, and in Resolve projects that don't use the
built-in brush, paint on the Mask input where the original should be kept. Painted areas show
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
- While it runs, **Calculation** shows the progress and the button becomes
  **Cancel calculation**. Maps that are already saved are reused. On Windows a
  small panel also shows the progress.
- Saved mattes are reused automatically while the shot and cleaning settings
  stay the same. Changing **Confidence** updates the result immediately without
  recalculating.
- Default matte folders:
  - macOS: `~/Library/Application Support/FabioColor/AdvancedDirtRemoval/Recovery`
  - Windows: `%LOCALAPPDATA%\FabioColor\AdvancedDirtRemoval\Recovery`
  - Linux: `$XDG_DATA_HOME/fabiocolor/advanced-dirt-removal/recovery` (or
    `~/.local/share/...`)

  Each shot is saved in its own folder. Delete old folders anytime to free
  space; they are recalculated when needed.

**Editing the matte.** In Resolve, Paint gives the original back and Erase
keeps the repair where the model gave back too much. With the Mask input, set
to Recovery, the mask edits the matte: unpainted areas follow the matte,
**white** strokes give the original back, **black** strokes keep the repair (take
back something the model gave back). Use **View: Auto recovery matte** to see the
matte.

### Spanish dirt removal

Manual frame-by-frame dust cleaning, applied only where you paint or mask. Go through
the shot frame by frame; where you see a speck you
want gone, paint a small mask over it on that frame. Only the painted spot is
repaired, from the neighbouring frames; everything else, and every frame you
don't paint, keeps the original. With the built-in brush, frames without strokes aren't cleaned at all, so
painting is fast. Spanish dirt removal does not use auto recovery. In Resolve,
auto recovery pauses while Spanish is selected and returns when you switch back
to Recovery; in other hosts, turn Auto recovery off first. With nothing painted,
Spanish dirt removal leaves the picture untouched.

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
| Difference threshold (%) | 0.1 | Changes smaller than this are ignored. 0.1% shows only real repairs; 0 shows every tiny rounding difference. |
| Red difference intensity | 8 | Brightness of the red highlights. |
| Red highlight opacity | 0.85 | Opacity of the red highlights. |
| Grayscale background | 1 | Brightness of the grey picture behind the highlights (0 = black). |
| Difference gain | 4 | Gain of the True difference view. |

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
- **Memory.** To keep painting and scrubbing responsive, the plugin caches
  cleaned frames in RAM (automatically capped at up to 20% of system memory).
  Set `ADR_FRAME_CACHE_MB` to override the cap, or `0` to turn caching off.
- **Spanish works frame by frame.** Each painted frame is cleaned once and
  keeps its repair while its own picture is unchanged, so changes on other
  frames don't affect it.
- **Auto recovery mattes** are reused while the shot and cleaning settings stay
  the same. If you change an upstream grade or effect before ADR, point **Matte
  folder** to a fresh folder before clicking **Calculate recovery**.
- **Interlaced material** is not supported; deinterlace first.

## Host notes

- **DaVinci Resolve, Fusion page:** tested with Resolve Studio 21 on macOS,
  Windows and Linux. Fusion shows all controls on one page; it does not use
  the effect's pages. A mask made at another resolution covers the same
  picture (Fusion is resolution-independent).
- **DaVinci Resolve, Color and Edit pages:** viewer painting tested with
  Resolve Studio 21 on macOS and Windows. Resolve 21.1 turns off the Enhanced
  Viewer on the Edit page; paint in its standard viewer.
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
