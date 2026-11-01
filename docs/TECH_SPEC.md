# Technical specification: the Fusion macros

Version 2.0. Two Fusion `MacroOperator` files for DaVinci Resolve:

- `Advanced Dirt Removal.setting`: standard footage (Rec.709, sRGB and similar).
- `Advanced Dirt Removal Cineon.setting`: Cineon log film scans; converts log to
  linear on the way in and back to log on the way out.

Primary use is the Fusion page. On the Edit/Cut pages the macros work as Fusion
effects with the motion mask; the recovery brush needs the Fusion page.

## What changed in 2.0

The dirt removal inside both macros is now the **Advanced Dirt Removal OFX
effect** (`ofx.com.fabiocolor.advanceddirtremoval`, node `DirtRemoval1`) in place
of Resolve's built-in Automatic Dirt Removal. Masking, the recovery brush, the
motion mask and the difference view are wired exactly as before. The effect's
controls are published on the macro in place of the old dirt-removal controls.

With no mask connected and Recovery type left at Recovery Brush, the macro's RGB
view is byte-identical to the effect on its own (checked in Resolve Studio 21.1 on
macOS, 56 frames of a real film scan).

## Node graph

```
MainInput1 ─[Cineon: CineonLog1, log → linear]─ CustomFilter1 ─┬─ DirtRemoval1 (EffectMask ← Switch1_2) ─┬─ Merge1 (over source) ─┐
                                                               │                                         └─ Merge2 (difference) ─┴─ Switch1 ─[Cineon: CineonLog1_1, linear → log]─ MainOutput1
                                                               └─ Deflicker1 → NoiseReduction1 → OpticalFlow1 → CustomTool1 (motion magnitude)
MainInput2 (Mask Paint / Magic Mask) → ChannelBooleans1 ─┬──────────────────────────────── Switch1_2 input 0 "Recovery Brush"
                                                         └─ ChannelBooleans1_2 (+ motion) ─ Switch1_2 input 1 "Motion Mask"
```

- `CustomFilter1` has every channel switched off and passes the image through.
- `Deflicker1`, `NoiseReduction1` and `OpticalFlow1` (Resolve tools) only build the
  motion mask; they never touch the repaired picture.
- `Switch1_2` is the effect mask of `DirtRemoval1`, applied inverted by default:
  masked (painted or moving) areas keep the source.
- `Merge1` is the final picture; `Merge2` is a difference view of the source and
  the repair.

## Published controls

| Control | Maps to | Default |
| --- | --- | --- |
| Input (MainInput1) | `CustomFilter1.Input` (Cineon: `CineonLog1.Input`) | — |
| Sensitivity, Motion guard, Repair amount, Analyse, Remove dark spots, Remove light spots, Extra sensitivity, Limit spot size, Largest spot (px), Blend repairs, Blend amount, Shot changes, Shot-change guard, Flash guard, Extended-range input, Analysis detail | `DirtRemoval1.<control>` | the effect's defaults; the Cineon macro sets **Extended-range input** on, because the effect sees linear values above 1 |
| Recovery/Spanish Dirt Removal | `DirtRemoval1.ApplyMaskInverted` | 1 (paint protects the source) |
| Recovery type | `Switch1_2.Source` | 0 = Recovery Brush, 1 = Motion Mask |
| Mask Paint/Magic Mask (MainInput2) | `ChannelBooleans1.Background` | — |
| Motion Mask Safety | `OpticalFlow1.Advanced.NumIters` | 2 |
| View Mode | `Switch1.Source` | 1 = Difference View; 0 = the repaired picture |
| Difference Intensity | `Merge2.Blend` | 0.6 (Cineon 0.9) |

The effect's controls are described in the [README](../README.md#the-advanced-dirt-removal-effect).

## Behaviour notes

- **Recovery brush:** connect a Mask Paint (or Magic Mask) tool to the "Mask
  Paint/Magic Mask" input. With Recovery polarity, painted areas keep the
  original; with Spanish polarity, only painted areas are repaired.
- **Motion mask:** repairs are limited to areas without motion. Painted masks
  still combine with it.
- **Difference view:** shows what changed; raise Difference Intensity for
  low-contrast material.
- **Cineon:** both CineonLog nodes are fixed to the Cineon curve. Areas the mask
  keeps unrepaired still pass through the log → linear → log round trip.

## Requirements

- DaVinci Resolve 18 or later with the Advanced Dirt Removal OFX effect installed
  (the installers add both).
- The motion mask uses Resolve's Deflicker, Noise Reduction and Optical Flow
  tools; Resolve Studio is recommended for them.
