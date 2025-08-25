# Advanced Dirt Removal for DaVinci Resolve

Professional dirt removal macro for DaVinci Resolve that combines automatic detection with dual recovery modes.

## Features

- **ResolveFX VideoRestoration** - Professional dirt removal engine
- **Pre-processing Chain** - Deflicker + NoiseReduction for enhanced quality
- **Motion Analysis** - Optical flow for automatic motion masking
- **Dual Recovery Modes** - Manual Paint Brush OR automatic Motion Mask
- **Diagnostic Views** - RGB and Difference modes for verification
- **Magic Mask Compatible** - External mask input for advanced workflows

## Installation

**One-click install (macOS):**
```bash
./scripts/install_one_click_mac.command
```

**Manual install:**
```bash
python3 scripts/install_advanced_dirt_removal.py
```

**Cross-platform scripts available for Windows and Linux.**

## Usage

1. Add "Advanced Dirt Removal" macro to your Fusion flow
2. Connect source to main input (auto-connects)
3. Optional: Connect Paint Mask or Magic Mask to second input
4. Choose Recovery Method: Paint Brush (manual) or Motion Mask (auto)
5. Use View Mode to switch between RGB (final) and Difference (diagnostic)

## Documentation

- [Installation Guide](docs/INSTALLATION.md) - Setup instructions
- [Usage Guide](docs/USAGE.md) - Complete workflow
- [Technical Specification](docs/TECH_SPEC.md) - Implementation details

## Requirements

- DaVinci Resolve 18+ (Studio recommended for advanced motion estimation)
- ResolveFX plugins (VideoRestoration, Deflicker, NoiseReduction)
