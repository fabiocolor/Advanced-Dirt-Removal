# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

This repository creates a **professional dirt removal macro** for DaVinci Resolve Fusion. The core system combines ResolveFX VideoRestoration with sophisticated preprocessing and dual recovery modes in a single MacroOperator.

## Architecture

**Single Macro System** - `Fusion/Advanced Dirt Removal.setting`:
```
Source → CustomFilter1 → Deflicker1 → NoiseReduction1 → OpticalFlow1 → AutomaticDirtRemoval1
                                                              ↓
                                                         CustomTool1 → ChannelBooleans → Switch1_2
                                                              ↑                              ↓
                                                    (Motion Analysis)              (Recovery Selector)
                                                                                        ↓
MainInput1 (Source) ←─────────────────────────────────────────────────────────→ Merge → Switch1 → Output
MainInput2 (Recovery Mask) ←─────────────────────────────────────────────────────────────↑
                                                                                     (View Mode)
```

**Key Components:**
- **Processing Chain**: CustomFilter1 → Deflicker1 → NoiseReduction1 → OpticalFlow1 → AutomaticDirtRemoval1
- **Motion System**: OpticalFlow1 → CustomTool1 → ChannelBooleans for automatic mask generation
- **Recovery System**: Switch between external Paint Mask OR internal Motion Mask
- **View System**: Switch between RGB (final) and Difference (diagnostic) outputs

## Development Commands

**Installation:**
```bash
# Quick install (macOS)
./scripts/install_one_click_mac.command

# Manual install (all platforms)
python3 scripts/install_advanced_dirt_removal.py

# Test installation without changes
python3 scripts/install_advanced_dirt_removal.py --dry-run
```

**Development Workflow:**
```bash
# 1. Edit the macro file directly
# Fusion/Advanced Dirt Removal.setting

# 2. Install to test
python3 scripts/install_advanced_dirt_removal.py

# 3. Test in DaVinci Resolve/Fusion
# MediaIn → Advanced Dirt Removal → MediaOut
```

**Installation Paths by OS:**
- **macOS**: `~/Library/Application Support/Blackmagic Design/DaVinci Resolve/Fusion/Macros/`
- **Windows**: `%APPDATA%\Blackmagic Design\DaVinci Resolve\Support\Fusion\Macros\`
- **Linux**: `$HOME/.local/share/DaVinciResolve/Fusion/Macros/`

## MacroOperator Structure

**Critical Implementation Details:**

**Input Connections:**
- `MainInput1` → CustomFilter1.Input (source - auto-connects)
- `MainInput2` → ChannelBooleans1.Background (recovery mask - optional)

**Parameter Mirroring Pattern:**
```lua
Input4 = InstanceInput {
    SourceOp = "AutomaticDirtRemoval1",
    Source = "temporalThreshold",    -- Must match exact OFX parameter name
    Default = 0.8,
},
```

**ResolveFX Integration:**
- `AutomaticDirtRemoval1` = `ofx.com.blackmagicdesign.resolvefx.VideoRestoration`
- `Deflicker1` = `ofx.com.blackmagicdesign.resolvefx.Deflicker`  
- `NoiseReduction1` = `ofx.com.blackmagicdesign.resolvefx.NoiseReduction`

## Key Implementation Rules

**MacroOperator vs GroupOperator**: Always use MacroOperator structure with MainInput1/MainInput2 pattern for proper auto-connection behavior.

**Parameter Path Precision**: 
- OpticalFlow Aggressiveness: `Source = "Advanced.NumIters"` (not "Method2.NumIters")
- ResolveFX parameters must match exact OFX identifiers from working examples

**Recovery System Logic**:
- Switch1_2 selects between Recovery Brush (0) and Motion Mask (1)
- ApplyMaskInverted=1 allows painting to reveal original pixels
- ChannelBooleans1 processes external mask input, ChannelBooleans1_2 processes motion mask

**No Compilation Required**: Direct text editing of `.setting` files. Restart DaVinci Resolve to reload macros.

## Testing Approach

**Manual Workflow**:
1. Install macro using installer script
2. Open DaVinci Resolve → Fusion page
3. Create flow: MediaIn → Advanced Dirt Removal → MediaOut
4. Test auto-connection of source (MainInput1)
5. Optional: Connect Paint Mask to second input (MainInput2)
6. Verify all parameters affect output correctly
7. Test Recovery type switching (Paint Brush vs Motion Mask)
8. Test View Mode switching (RGB vs Difference)

## Reference Materials

**Ground Truth Sources** (check before implementing):
- `Developer/` - Official Blackmagic Design SDK documentation and examples
- Working macro demonstrates exact parameter names and OFX identifiers
- `docs/TECH_SPEC.md` - Complete technical architecture

**Requirements**:
- DaVinci Resolve 18+ (Studio recommended for Advanced optical flow)
- ResolveFX plugins: VideoRestoration, Deflicker, NoiseReduction
- Adequate GPU memory for complex processing chain