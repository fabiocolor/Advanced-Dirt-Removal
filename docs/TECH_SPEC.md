# Technical specification: the Fusion macro

Version 2.0. One Fusion `MacroOperator`, `Advanced Dirt Removal.setting`, for
DaVinci Resolve, used for both video and Cineon log footage. It replaces the 1.x
Linear and Cineon macros and their optical-flow motion mask.

## Repair path

```
Source ─ SourcePlate ─┬─ Video: ────────────────────────────── DirtRemoval1 ─────────────────────────────┐
                      └─ Cineon: CineonLog1 (log → linear) ─── DirtRemoval1 ─ CineonDelta (back to log) ──┴─ OutputEncoding ─┐
RepairResult = Source + (Repair − Source) × gate × Applied repair amount  ←───────────────────────────────────────────────────┘
gate = RepairGate(Recovery mode × Mask Paint, rectangle, frame range)
```

- `DirtRemoval1` is the Advanced Dirt Removal OFX effect. Its Extended-range
  input follows **Input encoding** (on for Cineon, whose linear values exceed 1).
- In Cineon mode `CineonDelta` adds only the effect's change, converted back to
  log, to the original log values: `Source + log(Repair) − log(Input)`, and the
  exact source wherever the effect changed nothing. The log → linear → log round
  trip therefore never touches pixels the effect left alone.
- `RepairResult` keeps the source exactly where the gate is 0, uses the repair
  exactly where it is 1, and blends in between. With the defaults, video
  encoding and no mask, the macro is byte-identical to the effect alone.
- **Recovery mode** (`RepairGate.NumberIn1`): *Ignore mask*; *Mask Paint
  recovery* (default: painted areas keep the original); *Apply repair inside
  mask* (Spanish dirt removal: only painted areas are repaired). The mask is the
  alpha of the second input, so Mask Paint or Magic Mask connect directly.
- **Limit to rectangle** (centre, size and feather in normalised image
  coordinates) and **Limit to frame range** (Fusion frame numbers, both ends
  included) restrict the gate further.
- Moving detail is handled by the effect's **Motion guard** and the brush. The
  1.x optical-flow motion mask was removed: on fast hand motion it was about 12
  times slower and no better than raising Motion guard.

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

Thresholds compare RGB values in the input encoding. The effect moves pixels it
does not repair by less than one 10-bit code value, so the threshold starts at
0.1%; set 0 to see every change.

## Published controls

| Page | Controls |
| --- | --- |
| Controls | Source, Mask Paint/Magic Mask, Input encoding; Analyse, Remove dark spots, Remove light spots, Sensitivity, Extra sensitivity (detection); Repair amount (processing); Motion guard (safeguards); View Mode, Difference threshold (%), Red difference intensity |
| Advanced | Analysis detail, Limit spot size, Largest spot (px) (detection); Blend repairs, Blend amount (processing); Shot changes, Shot-change guard, Flash guard (safeguards); Recovery mode, Applied repair amount, Difference gain, Red highlight opacity, Grayscale background, Wipe position, frame-range and rectangle limits |

The effect's controls are described in the [README](../README.md#the-advanced-dirt-removal-effect).

## Requirements

DaVinci Resolve 18 or later with the Advanced Dirt Removal OFX effect installed
(the installers add both). The macro no longer needs Resolve Studio's
Deflicker, Noise Reduction or Optical Flow tools.
