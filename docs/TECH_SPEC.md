# Technical Specification

**Advanced Dirt Removal** - Professional dirt removal macro for DaVinci Resolve  
Version: 1.0 (Production Ready)

## Architecture

**MacroOperator Structure** - Single `.setting` file containing:

### Processing Chain
1. **CustomFilter1** - Input conditioning
2. **Deflicker1** - ResolveFX temporal stabilization 
3. **NoiseReduction1** - ResolveFX spatial/temporal noise reduction
4. **OpticalFlow1** - Motion vector calculation (Advanced method)
5. **CustomTool1** - Motion magnitude visualization
6. **AutomaticDirtRemoval1** - ResolveFX VideoRestoration (core engine)

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
- **Aggressiveness** - OpticalFlow NumIters (Default: 2)
- Controls motion detection sensitivity

### Dirt Removal
- **motionEstType** - Motion estimation method
- **supportLength** - Temporal support window
- **temporalThreshold** - Detection threshold (Default: 0.8)
- **dirtSize** - Minimum defect size (Default: 0.01)
- **motionThreshold** - Motion sensitivity (Default: 25.2)

### Recovery
- **Recovery type** - Switch between manual/automatic
- **View Mode** - RGB/Difference output selection

## Controls & Published Inputs

This section lists the macro's published (InstanceInput) controls and how they map to internal operators (names taken directly from `Advanced Dirt Removal.setting`). Use these names when looking at the macro's Inspector in Fusion.

- `Aggressiveness` (maps to `OpticalFlow1.Advanced.NumIters`) — default: 2
- `MainInput1` — Source image (maps to `CustomFilter1.Input`) — this is the main input for MediaIn in Fusion.
- `Recovery type` (maps to `Switch1_2.Source`) — select between "Recovery Brush" and "Motion Mask".
- `Mask Paint/Magic Mask` (maps to `ChannelBooleans1.Background`) — external painted mask input (published as `MainInput2`).
- `Recovery/Spanish Dirt Removal` (maps to `AutomaticDirtRemoval1.ApplyMaskInverted`) — toggles brush polarity (see Behavior Notes).
- `View Mode` (maps to `Switch1.Source`) — toggles RGB vs Difference (diagnostic) view.

Other key internal parameters published for convenience:
- `motionEstType`, `supportLength`, `temporalThreshold` (default 0.8), `dirtSize` (default 0.01), `showRepairMask` (default 0), `motionThreshold` (default 25.2), `edgeAttenuation` (default 0)

## Node wiring (logical)

The macro's internal node graph wires the automatic repair and recovery mask as follows (simplified):

- `CustomFilter1` (input conditioning) → background into `Merge1` and `Merge2`.
- `AutomaticDirtRemoval1` (ResolveFX VideoRestoration) → foreground into `Merge1`/`Merge2`.
- `Switch1_2` selects which mask is used as the `EffectMask` for `AutomaticDirtRemoval1`:
  - Option 0: `ChannelBooleans1` (Recovery Brush mask)
  - Option 1: `ChannelBooleans1_2` (Motion Mask / Motion-based mask)
- `Merge1` composites AutomaticDirtRemoval (Foreground) over Source (Background) for the final RGB result.
- `Merge2` is configured to produce a difference-style view (ApplyMode = Difference, Blend ~0.6) used by `Switch1` to offer RGB vs Difference outputs.
- `Switch1` selects between RGB (Merge1) and Difference (Merge2) and is published to `MainOutput1`.

## Behavior notes

- Spanish Dirt Removal (brush polarity): the `Recovery/Spanish Dirt Removal` control flips how painted pixels are interpreted:
  - When polarity = Recovery (paint protects the Source), painted regions bias the composite toward the original Source (useful to restore detail that the automatic pass removed).
  - When polarity = Spanish (paint-to-repair), painted regions instruct the VideoRestoration engine to apply repair where you paint (you are effectively painting the repair instead of painting a protection mask).
- Hybrid workflows: even in Motion Mask mode the user can add manual paints (the published mask input accepts painted masks), enabling a mix of automatic motion-based selection and manual correction.
- Difference View: use `View Mode` / `Switch1` to toggle Difference View for fast visual inspection; this aids deciding where to add recovery paints or change mask polarity.

## Defaults, Versions and Notes

- AutomaticDirtRemoval node advertises `resolvefxVersion = "1.0"` and is implemented with `ofx.com.blackmagicdesign.resolvefx.VideoRestoration`.
- Many ResolveFX internals (OpticalFlow advanced mode, some motion estimation options) are only fully available in DaVinci Resolve Studio.
- The macro's published inputs are intended to match the underlying OFX control names where relevant so users familiar with the ResolveFX UI will recognise them in Fusion.

## Requirements (updated)

- DaVinci Resolve Studio strongly recommended for Advanced optical flow and motion-estimation modes.
- ResolveFX Plugins: VideoRestoration, Deflicker, NoiseReduction (bundled with Resolve/Studio).
- Adequate GPU memory/VRAM for optical flow + temporal processing on high-resolution footage.
- For full mask-paint workflows use the Fusion page (the Edit and Color pages do not expose the Fusion Mask Paint input; Magic Mask is a usable alternative outside Fusion).

## Inputs/Outputs

### Inputs
- **MainInput1** - Source image (CustomFilter1.Input)
- **MainInput2** - Recovery mask (ChannelBooleans1.Background)

### Output  
- **MainOutput1** - Processed result (Switch1.Output)

## Requirements

- **DaVinci Resolve Studio** - Required for Advanced optical flow
- **ResolveFX Plugins** - VideoRestoration, Deflicker, NoiseReduction
- **GPU Memory** - Complex processing chain requires adequate VRAM