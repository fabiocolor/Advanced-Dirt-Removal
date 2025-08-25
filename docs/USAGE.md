# Usage Guide

## Basic Workflow

1. Add "Advanced Dirt Removal" macro to your Fusion flow
2. Connect source to main input (auto-connects)
3. Optional: Connect Paint Mask or Magic Mask to second input
4. Adjust parameters and recovery method as needed

## Parameters

### Motion Analysis
- **Aggressiveness** - Motion detection sensitivity (Default: 2)

### Dirt Removal
- **motionEstType** - Motion estimation method (Fast/Better/Best)
- **supportLength** - Temporal support window (1-5 frames)
- **temporalThreshold** - Detection sensitivity (Default: 0.8)
- **dirtSize** - Minimum defect size (Default: 0.01)
- **showRepairMask** - Display repair mask overlay
- **motionThreshold** - Motion sensitivity (Default: 25.2)
- **edgeAttenuation** - Edge softening

### Recovery
- **Recovery/Spanish Dirt Removal** - Invert mask behavior (Default: On)
- **Recovery type** - Switch between Recovery Brush (0) or Motion Mask (1)
- **View Mode** - RGB View (final) or Difference View (diagnostic)

## Recovery Methods

### Recovery Brush (Manual)
- Connect Paint Mask or Magic Mask to second input
- Paint over areas to reveal original pixels
- Use with "Recovery/Spanish Dirt Removal" enabled

### Motion Mask (Automatic)
- Uses optical flow analysis to create recovery mask
- Automatically recovers moving objects
- Good for protecting motion from over-processing

## View Modes

### RGB View
Shows final processed result

### Difference View
Shows diagnostic view highlighting changes made by dirt removal
- Useful for verifying removal quality
- Helps identify false positives/negatives

## Tips

### Quality Control
1. Start with conservative settings
2. Use Difference View to check results
3. Paint manually for problem areas
4. Adjust temporal threshold for material type

### Performance
- Use proxy media for faster feedback
- Cache results when satisfied with settings
- Studio version recommended for best motion estimation

### Workflow Integration
- Works anywhere in Fusion flow
- Compatible with color correction pipeline
- Real-time preview while adjusting