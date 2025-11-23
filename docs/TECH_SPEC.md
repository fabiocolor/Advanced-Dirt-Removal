# Technical Specification

**Advanced Dirt Removal** - Professional Fusion macros for DaVinci Resolve
Version: 1.1 (Current Release - Dual Version System)

**Type:** Fusion MacroOperator (.setting files)
**Two Specialized Versions:**
- **Linear Version**: Standard footage workflows
- **Cineon Version**: Film scan workflows with built-in color space conversion

**Primary Use:** Fusion page compositing workflows
**Secondary Use:** Edit/Cut page effects (with limitations - no Recovery Brush support)

## Architecture

**MacroOperator Structure** - Two specialized `.setting` files:

### Linear Version Processing Chain
1. **CustomFilter1** - Input conditioning
2. **Deflicker1** - ResolveFX temporal stabilization
3. **NoiseReduction1** - ResolveFX spatial/temporal noise reduction
4. **OpticalFlow1** - Motion vector calculation (Advanced method)
5. **CustomTool1** - Motion magnitude visualization
6. **AutomaticDirtRemoval1** - ResolveFX VideoRestoration (core engine)

### Cineon Version Processing Chain
1. **CineonLog1** - Cineon log to linear color space conversion
2. **CustomFilter1** - Input conditioning (linear space)
3. **Deflicker1** - ResolveFX temporal stabilization
4. **NoiseReduction1** - ResolveFX spatial/temporal noise reduction
5. **OpticalFlow1** - Motion vector calculation (Advanced method)
6. **CustomTool1** - Motion magnitude visualization
7. **AutomaticDirtRemoval1** - ResolveFX VideoRestoration (core engine)
8. **CineonLog2** - Linear to Cineon log color space conversion (output)

### Recovery System
- **Switch1_2** - Recovery method selector:
  - Recovery Brush (external mask input)
  - Motion Mask (automatic motion-based)
- **ChannelBooleans1/1_2** - Mask generation and processing

### View Modes
- **Switch1** - Output selector:
  - RGB View (final result)
  - Difference View (diagnostic)

## Key Parameters

### Motion Analysis
- **Motion Mask Safety** - OpticalFlow NumIters (renamed from "Aggressiveness") (Default: 2)
- Controls motion detection sensitivity
- Positioned after Recovery type for better workflow

### Dirt Removal - Linear Version Optimized
- **motionEstType** - Motion estimation method (HS_Better)
- **supportLength** - Temporal support window (SUPPORT_LENGTH_4)
- **temporalThreshold** - Detection threshold (Default: 0.45)
- **dirtSize** - Minimum defect size (Default: 0.01)
- **motionThreshold** - Motion sensitivity (Default: 25)

### Dirt Removal - Cineon Version Optimized
- **motionEstType** - Motion estimation method (HS_Better)
- **supportLength** - Temporal support window (SUPPORT_LENGTH_4)
- **temporalThreshold** - Detection threshold (Default: 0.5)
- **dirtSize** - Minimum defect size (Default: 0.01)
- **motionThreshold** - Motion sensitivity (Default: 55)

### Cineon Conversion (Cineon Version Only)
- **Input CineonLog**: Mode=0 (Log→Linear), LogType=Cineon, Depth=Auto
- **Output CineonLog**: Mode=1 (Linear→Log), LogType=Cineon, Depth=Auto

### Recovery
- **Recovery type** - Switch between manual/automatic
- **View Mode** - RGB/Difference output selection

## Controls & Published Inputs

This section lists the macro's published (InstanceInput) controls and how they map to internal operators (names taken directly from `Advanced Dirt Removal v1.2.setting`). Use these names when looking at the macro's Inspector in Fusion.

- `Aggressiveness` (maps to `OpticalFlow1.Advanced.NumIters`) — default: 2
- `MainInput1` — Source image (maps to `CineonLog1.Input`) — this is the main input for MediaIn in Fusion.
- `Recovery type` (maps to `Switch1_2.Source`) — select between "Recovery Brush" and "Motion Mask".
- `Mask Paint/Magic Mask` (maps to `ChannelBooleans1.Background`) — external painted mask input (published as `MainInput2`).
- `Recovery/Spanish Dirt Removal` (maps to `AutomaticDirtRemoval1.ApplyMaskInverted`) — toggles brush polarity (see Behavior Notes).
- `View Mode` (maps to `Switch1.Source`) — toggles RGB vs Difference (diagnostic) view.
- `Difference Intensity` (maps to `Merge2.Blend`) — default: 0.6 — adjusts visibility of difference view.

**Note:** v1.2 has no user control for log conversion - Cineon log conversion is always active.

Other key internal parameters published for convenience:
- `motionEstType`, `supportLength`, `temporalThreshold` (default 0.8), `dirtSize` (default 0.01), `showRepairMask` (default 0), `motionThreshold` (default 25.2), `edgeAttenuation` (default 0)

## Node wiring (logical) - v1.2

The macro's internal node graph wires the automatic repair and recovery mask as follows (simplified):

- `MainInput1` → `CineonLog1` (log to linear conversion, bypassed by default) → `CustomFilter1` (input conditioning)
- `CustomFilter1` → background into `Merge1` and `Merge2`
- `CustomFilter1` → `Deflicker1` → `NoiseReduction1` → `OpticalFlow1` → preprocessing chain
- `OpticalFlow1` → `CustomTool1` → motion magnitude calculation for motion mask
- `AutomaticDirtRemoval1` (ResolveFX VideoRestoration) → foreground into `Merge1`/`Merge2`
- `Switch1_2` selects which mask is used as the `EffectMask` for `AutomaticDirtRemoval1`:
  - Option 0: `ChannelBooleans1` (Recovery Brush mask)
  - Option 1: `ChannelBooleans1_2` (Motion Mask / Motion-based mask)
- `Merge1` composites AutomaticDirtRemoval (Foreground) over Source (Background) for the final RGB result
- `Merge2` is configured to produce a difference-style view (ApplyMode = Difference, Blend ~0.6) used by `Switch1` to offer RGB vs Difference outputs
- `Switch1` selects between RGB (Merge1) and Difference (Merge2)
- `Switch1` → `CineonLog2` (linear to log conversion, linked to CineonLog1.LogType) → `MainOutput1`

## Behavior notes

- **Cineon Log Conversion (v1.2)**: The v1.2 macro automatically handles Cineon log-encoded footage:
  - `CineonLog1` (LogType=1) converts Cineon log to linear at the beginning of the processing chain
  - All processing (optical flow, noise reduction, dirt removal) happens in linear color space for mathematical accuracy
  - `CineonLog2` (LogType=1) converts back to Cineon log at the end, maintaining log encoding for grading workflows
  - Both CineonLog nodes are hardcoded to Cineon format - no user control needed
  - Conversion is always active - use v1.1 for non-log footage
- **Spanish Dirt Removal (brush polarity)**: the `Recovery/Spanish Dirt Removal` control flips how painted pixels are interpreted:
  - When polarity = Recovery (paint protects the Source), painted regions bias the composite toward the original Source (useful to restore detail that the automatic pass removed).
  - When polarity = Spanish (paint-to-repair), painted regions instruct the VideoRestoration engine to apply repair where you paint (you are effectively painting the repair instead of painting a protection mask).
- **Hybrid workflows**: even in Motion Mask mode the user can add manual paints (the published mask input accepts painted masks), enabling a mix of automatic motion-based selection and manual correction.
- **Difference View**: use `View Mode` / `Switch1` to toggle Difference View for fast visual inspection; this aids deciding where to add recovery paints or change mask polarity.
- **Difference Intensity (v1.1+)**: adjust the `Difference Intensity` slider to control the visibility of the difference view, making it easier to see changes in low-contrast footage.

## Defaults, Versions and Notes

- AutomaticDirtRemoval node advertises `resolvefxVersion = "1.0"` and is implemented with `ofx.com.blackmagicdesign.resolvefx.VideoRestoration`.
- Many ResolveFX internals (OpticalFlow advanced mode, some motion estimation options) are only fully available in DaVinci Resolve Studio.
- The macro's published inputs are intended to match the underlying OFX control names where relevant so users familiar with the ResolveFX UI will recognise them in Fusion.

## Requirements

- **DaVinci Resolve Studio** strongly recommended for Advanced optical flow and motion-estimation modes
- **ResolveFX Plugins**: VideoRestoration, Deflicker, NoiseReduction (bundled with Resolve/Studio)
- **GPU memory/VRAM**: Adequate for optical flow + temporal processing on high-resolution footage
- **Fusion page**: Required for full functionality including Recovery Brush workflows (Edit/Cut pages do not expose Mask Paint input connections)

## Inputs/Outputs

### Inputs (v1.2)
- **MainInput1** - Source image (CineonLog1.Input) - automatically connects to MediaIn
- **MainInput2** - Recovery mask (ChannelBooleans1.Background) - optional Mask Paint/Magic Mask input

### Output (v1.2)
- **MainOutput1** - Processed image (CineonLog2.Output) - with optional log conversion applied

## Requirements

- **DaVinci Resolve Studio** - Required for Advanced optical flow
- **ResolveFX Plugins** - VideoRestoration, Deflicker, NoiseReduction
- **GPU Memory** - Complex processing chain requires adequate VRAM