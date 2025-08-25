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