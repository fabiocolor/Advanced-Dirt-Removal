# Advanced Dirt Removal for Resolve Fusion - AI Coding Guide

## Project Architecture

This project creates a **Fusion macro** (`.setting` file) for DaVinci Resolve that combines automatic dirt removal with manual recovery tools. The core architecture follows this pipeline:

```
Source → AutomaticDirtRemoval (ResolveFX) → Merge ← Original Source
                                              ↑
                                         PaintMask (recovery brush)
```

**Key Components:**
- `Fusion/Advanced Dirt Removal.setting` - The main deliverable (Fusion Group/Macro)
- `scripts/` - Installation utilities and helper scripts  
- `docs/` - Product specs (PRD, TECH_SPEC, ARCHITECTURE) that drive implementation
- `Developer/` - Blackmagic SDK reference materials (read-only)

## Development Workflow

**No compilation required** - this is a Fusion macro project:
1. Edit the `.setting` file directly (Fusion text format)
2. Copy to Fusion Macros folder: `~/Library/Application Support/Blackmagic Design/DaVinci Resolve/Fusion/Macros/`
3. Restart Resolve/Fusion to reload
4. Test in Fusion: MediaIn → Advanced Dirt Removal → MediaOut

**Quick install commands:**
```bash
# macOS
./scripts/install_one_click_mac.command

# Test installation
python3 scripts/install_advanced_dirt_removal.py --dry-run
```

## Fusion Macro Structure

The `.setting` file defines a **MacroOperator** with three critical sections:

**Inputs (InstanceInput)** - Published controls on the macro UI:
- Tab 1: Mirror ALL ResolveFX AutomaticDirtRemoval parameters 1:1 (names must match exactly)
- Tab 2: Paintback controls (Brush Size, Softness, Invert)
- Tab 3: Settings (Bypass)

**Tools** - Internal node graph:
- `AutomaticDirtRemoval1` - The core ResolveFX VideoRestoration node (ofx.com.blackmagicdesign.resolvefx.VideoRestoration)
- `Merge1` - Composite (Background=Source, Foreground=cleaned, EffectMask=recovery mask)

For separate RecoveryBrush macro:
- `PaintMask1` - Recovery brush (locked to Color mode, Multistroke enabled, built-in softness)

**Critical naming pattern**: Use BMD-compliant MainInput1/MainInput2 structure for MacroOperator inputs.

## Key Implementation Rules

**Parameter Mirroring**: Expose ResolveFX parameters with EXACT names from the underlying OFX:
```lua
MotionEstimation = InstanceInput {
    SourceOp = "AutomaticDirtRemoval1",
    Source = "motionEstType",  -- Must match OFX control name exactly
    Name = "Motion Estimation",
    Page = "Automatic Dirt Removal",
},
```

**Paint Tool Configuration**: RecoveryBrush macro enforces specific paint settings:
- `ApplyMode = "PaintApplyColor"` - Paint into alpha channel (locked)
- `Multistroke` enabled for single-frame strokes (locked)
- Built-in softness control (no separate blur needed)

**Merge Wiring**: Background=Source, Foreground=AutoDirtRemoval, EffectMask=RecoveryMask (from external RecoveryBrush)

## Version Strategy

- **v0**: Documentation and specs (complete)
- **v1**: 2-macro implementation (complete)
  - Advanced Dirt Removal macro with ResolveFX VideoRestoration
  - RecoveryBrush macro (simplified MaskPaint wrapper)
- **v2**: Optional GPU acceleration enhancements

**Ground Truth Approach**: Always check `Developer/` directory and working examples like `HISTORY BRUSH.comp` before implementing.

## Testing Approach

Manual testing workflow (no automated tests):
1. Load both macros in Fusion: MediaIn → Advanced Dirt Removal → MediaOut
2. Connect MediaIn → RecoveryBrush, RecoveryBrush Mask → Advanced Dirt Removal Recovery Mask
3. Tab 1: Verify all ResolveFX controls match VideoRestoration behavior
4. RecoveryBrush: Paint strokes should create mask for recovery
5. Test Invert toggle, Recovery Opacity, Bypass modes

**Reference files**: 
- `HISTORY BRUSH.comp` - Working ResolveFX implementation
- `Developer/` directory - BMD official documentation and examples

## Cross-Platform Considerations

**Install paths vary by OS** - use `docs/INSTALLATION.md` reference:
- macOS: `~/Library/Application Support/Blackmagic Design/DaVinci Resolve/Fusion/Macros/`
- Windows: `%APPDATA%/Blackmagic Design/DaVinci Resolve/Support/Fusion/Macros/`
- Linux: `$HOME/.local/share/DaVinciResolve/Fusion/Macros/`

**Python scripts**: Use stdlib only, Python 3 compatible, snake_case naming.

## Integration Points

**ResolveFX VideoRestoration**: The actual dirt removal engine (ofx.com.blackmagicdesign.resolvefx.VideoRestoration)
- Parameters documented in `Developer/OpenFX/` 
- Working implementation in `HISTORY BRUSH.comp`

**2-Macro Architecture**: 
- Advanced Dirt Removal: Real ResolveFX wrapper with external mask input
- RecoveryBrush: Simplified MaskPaint wrapper for recovery painting

**Studio vs Free**: Motion estimation features require Resolve Studio; macros degrade gracefully.

## Common Pitfalls

- **Wrong OFX identifiers**: Always use `ofx.com.blackmagicdesign.resolvefx.VideoRestoration`, not custom names
- **GroupOperator vs MacroOperator**: BMD uses MacroOperator structure with MainInput1/MainInput2 pattern
- **Circular dependencies**: Never connect nodes in loops (removed Blur operations for this reason)
- **Parameter name mismatches**: Check working examples in `HISTORY BRUSH.comp` for exact parameter names
- **Paint mode enforcement**: RecoveryBrush locks Color mode - no user configuration needed
- **Path assumptions**: Never hardcode install paths - use the provided installer scripts
- **Documentation-first rule**: Always check `Developer/` directory before implementing - avoid reinventing BMD tools
