# Technical specification: the Fusion macro

Version 2.0. One Fusion `MacroOperator`, `Advanced Dirt Removal.setting`, for
DaVinci Resolve, used for both video and Cineon log footage. It replaces the 1.x
Linear and Cineon macros.

## Repair path

```
Source ── SourcePlate ──┬── DirtRemoval1 (Advanced Dirt Removal effect) ──┐
                        │                                                  RepairResult = Source + (Repair − Source) × gate × Applied repair amount
                        └──────────────────────────────────────────────────┘
gate = RepairGate(Recovery mode × Mask Paint, rectangle, frame range, 1 − motion protection)
```

- `DirtRemoval1` is the Advanced Dirt Removal OFX effect. It works on the values
  as delivered, in either encoding, so no colour conversion touches the repair.
- `RepairResult` keeps the source exactly where the gate is 0, uses the repair
  exactly where the gate is 1 and blends in between. With the defaults and no
  mask connected the macro's Restored view is byte-identical to the effect alone.
- **Recovery mode** (`RepairGate.NumberIn1`): *Ignore mask*; *Mask Paint
  recovery* (default: painted areas keep the original); *Apply repair inside
  mask* (only painted areas are repaired). The mask is the alpha of the second
  input, so Mask Paint or Magic Mask connect directly.
- **Limit to rectangle** (centre, size and feather in normalised image
  coordinates) and **Limit to frame range** (Fusion frame numbers, both ends
  included) restrict the gate further.

## Motion protection (optional)

Off by default (**Protect moving areas**). When on:

- `InputEncoding` converts Cineon log to linear for this branch only;
  `Deflicker1 → NoiseReduction1 → OpticalFlow1` (Resolve tools) give forward
  and backward vectors, `MotionSpeed`/`MotionAverage` turn them into a smoothed
  speed in pixels per frame at 2K width.
- `MotionPrevious`/`MotionNext` (clamped to the composition range) and
  `MotionTemporal` add the colour change between the neighbouring frames, so a
  dirt hit in the current frame alone does not count as motion.
- `MotionProtection` combines both with smooth thresholds (**Motion
  sensitivity**, **Ignore small motion**, **Motion transition**, **Temporal motion
  threshold/transition**), and `MotionMask` softens the matte. White keeps the
  original, black allows repair.

It is a heuristic: it can react to grain, occlusions or gate movement and can
miss difficult motion. Check it with **View Mode → Motion protection** and use the
brush where needed. It adds substantial optical-flow processing time.

## Views (View Mode)

| View | Shows |
| --- | --- |
| Restored | the result (default) |
| Original | the input |
| Compare wipe | original left of **Wipe position**, result right |
| True difference | absolute difference × **Difference gain** |
| Red difference | grey picture (**Grayscale background**, 0 = black) with changes above **Difference threshold (%)** in red, scaled by **Red difference intensity** and **Red highlight opacity** |
| Repair eligibility | the gate: white = repair allowed |
| Changed pixels | largest channel change × Red difference intensity |
| Motion protection | the motion matte: white = protected |

Thresholds compare RGB values in the input encoding. The effect moves pixels it
does not repair by less than one 10-bit code value, so the threshold starts at
0.1%; set 0 to see every change.

## Published controls

| Page | Controls |
| --- | --- |
| Controls | Source, Mask Paint/Magic Mask, Input encoding, Sensitivity, Motion guard, Repair amount, Analyse, Remove dark spots, Remove light spots, Extra sensitivity, Protect moving areas, View Mode, Difference threshold (%), Red difference intensity |
| Advanced | Limit spot size, Largest spot (px), Blend repairs, Blend amount, Shot changes, Shot-change guard, Flash guard, Extended-range input, Analysis detail, Recovery mode, Applied repair amount, motion-protection tuning, Optical flow iterations, Difference gain, Red highlight opacity, Grayscale background, Wipe position, frame-range and rectangle limits |

The effect's controls are described in the [README](../README.md#the-advanced-dirt-removal-effect).

## Requirements

- DaVinci Resolve 18 or later with the Advanced Dirt Removal OFX effect installed
  (the installers add both).
- Motion protection uses Resolve's Deflicker, Noise Reduction and Optical Flow
  tools; Resolve Studio is recommended for it.
