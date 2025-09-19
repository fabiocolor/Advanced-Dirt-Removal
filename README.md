# Advanced Dirt Removal for DaVinci Resolve

[![Version](https://img.shields.io/badge/version-v1.0-blue?style=flat-square)](https://github.com/fabiocolor/Dirt-Removal-Recovery/releases)
[![License](https://img.shields.io/badge/license-MIT-green?style=flat-square)](LICENSE)
[![DaVinci Resolve](https://img.shields.io/badge/DaVinci%20Resolve-18%2B-red?style=flat-square)](https://www.blackmagicdesign.com/products/davinciresolve)
[![Platform](https://img.shields.io/badge/platform-macOS%20%7C%20Windows%20%7C%20Linux-lightgrey?style=flat-square)](#installation)

[![Download](https://img.shields.io/badge/Download-Latest%20Release-brightgreen?style=for-the-badge&logo=download)](https://github.com/fabiocolor/Dirt-Removal-Recovery/releases/latest)

---

**Advanced Dirt Removal** is a Fusion macro that improves DaVinci Resolve's restoration capabilities, making it a viable low-cost platform for archival and educational film restoration projects. It extends the built-in Automatic Dirt Removal with motion-masking, manual recovery tools, and diagnostic views to handle challenging restoration work without expensive specialized software.

![Recovery Brush Workflow](docs/images/Recovery%20brush.png)
*Manual recovery brush workflow for precise restoration control*

> 🎯 **Perfect for**: Educational institutions, archives, non-commercial restoration projects, and making film preservation more accessible

---

## ✨ Key Features

**🔄 Accessible Recovery Options**
- **Motion Mask**: Automatically restricts repairs to low/no-motion areas
- **Recovery Brush**: Manual painting to restore original details when automatic fails

**🎨 Enhanced Processing**
- Leverages existing DaVinci Resolve tools (optical flow, noise reduction, deflicker)
- Uses built-in ResolveFX VideoRestoration engine
- No additional software purchases required

**👁️ Educational Diagnostics**
- **Difference View**: See exactly what's being changed
- **Spanish Dirt Removal**: Learn different brush techniques
- **Iterative Workflow**: Understand and refine restoration decisions

---

## 📦 Installation

### Quick & Easy Setup

1. **📥 Download** the `Advanced Dirt Removal.setting` file from the [latest release](https://github.com/fabiocolor/Dirt-Removal-Recovery/releases)

2. **📂 Locate** your DaVinci Resolve Effects folder:

| Platform | Installation Path |
|----------|-------------------|
| **🍎 macOS** | `~/Library/Application Support/Blackmagic Design/DaVinci Resolve/Fusion/Templates/Edit/Effects` |
| **🪟 Windows** | `%APPDATA%\Blackmagic Design\DaVinci Resolve\Support\Fusion\Templates\Edit\Effects` |
| **🐧 Linux** | `~/.local/share/DaVinciResolve/Fusion/Templates/Edit/Effects` |

3. **📋 Copy** the `.setting` file to the Effects folder

4. **🔄 Restart** DaVinci Resolve

> 💡 **Tip**: On macOS, press `Cmd+Shift+G` in Finder and paste the path. On Windows, paste the path in Explorer's address bar.

---

## 🚀 Usage

### Basic Workflow

![Motion Mask Mode](docs/images/Motion%20Mask.png)
*Automatic motion-based dirt removal restricting repairs to stationary areas*

1. **Add the Effect**
   - **Fusion Page**: `Add Tool > Macros > Advanced Dirt Removal`
   - **Edit/Cut Pages**: `Effects > Templates > Advanced Dirt Removal`

2. **Connect Your Source**
   - Drag from MediaIn to Advanced Dirt Removal input

3. **Choose Recovery Mode**
   - **Motion Mask**: Automatic motion-based protection
   - **Recovery Brush**: Manual painting control (requires mask connection - see below)

![Spanish Dirt Removal](docs/images/Spanish%20Dirt%20Removal.png)
*Spanish Dirt Removal mode - paint to target specific repairs*

4. **Fine-tune Parameters**
   - Adjust dirt detection sensitivity
   - Control motion threshold
   - Set temporal processing strength

### Advanced Techniques

**🎭 Spanish Dirt Removal Mode**
<!-- Screenshot placeholder: spanish-mode-comparison.jpg -->
<!-- *Recovery vs Spanish polarity on the same area* -->

> **📝 Note**: Requires Mask Paint tool connection (see Recovery Brush Workflow below)

- **Recovery Polarity** (Default): Paint to protect original details
- **Spanish Polarity**: Paint to target specific repairs
- Toggle the "Spanish Dirt Removal" checkbox to switch modes

**🔍 Difference View**
<!-- Screenshot placeholder: difference-view.jpg -->
<!-- *Real-time diagnostic showing modifications in red/green overlay* -->

- Toggle to see exactly what's being changed
- Perfect for iterative refinement

**🎨 Recovery Brush Workflow**
<!-- Screenshot placeholder: paint-workflow.jpg -->
<!-- *Manual refinement with visible paint strokes* -->

> **⚡ Required Setup**: To use Recovery Brush or Spanish Dirt Removal, you must connect a **Mask Paint** tool to the **labeled corrector input** on the Advanced Dirt Removal macro.

**Connection Steps:**
1. Add a **Mask Paint** tool to your Fusion composition
2. Connect the **Mask Paint output** to the **"Mask Paint/Magic Mask" input** on Advanced Dirt Removal
3. Set Recovery type to "Recovery Brush"
4. Paint your mask and see results in real-time

**Usage:**
- Paint to recover specific details
- Toggle Spanish Dirt Removal for different brush behavior
- Combine with automatic motion detection
- Real-time feedback with difference view

---

## 🎛️ Controls Reference

### Core Settings

| Control | Purpose | Default | Notes |
|---------|---------|---------|-------|
| **Recovery type** | Choose between automatic or manual recovery | Recovery Brush (0) | 0 = Recovery Brush, 1 = Motion Mask |
| **View Mode** | Toggle output between final result and diagnostic | RGB (1) | 0 = Difference View, 1 = RGB Output |

### Motion Analysis

| Control | Purpose | Default | Notes |
|---------|---------|---------|-------|
| **Aggressiveness** | Optical flow motion detection intensity | 2 | Higher = more detailed motion analysis |
| **motionThreshold** | Sensitivity for motion-based masking | 25.2 | Lower = more areas treated as motion |

### Dirt Detection

| Control | Purpose | Default | Notes |
|---------|---------|---------|-------|
| **temporalThreshold** | How aggressive the dirt detection is | 0.8 | Lower = more sensitive, detects smaller defects |
| **dirtSize** | Minimum size of defects to detect | 0.01 | Smaller = detects tinier dirt particles |
| **motionEstType** | Motion estimation algorithm | (varies) | Different methods for motion calculation |
| **supportLength** | Temporal window for analysis | (varies) | How many frames to analyze together |

### Advanced Options

| Control | Purpose | Default | Notes |
|---------|---------|---------|-------|
| **Recovery/Spanish Dirt Removal** | Toggle brush polarity behavior | Recovery (1) | 1 = Paint protects, 0 = Paint targets |
| **edgeAttenuation** | Reduce processing near edges | 0 | Helps avoid edge artifacts |
| **showRepairMask** | Display the repair mask | 0 | For diagnostic purposes |

### Input Connections

| Input | Purpose | Required |
|-------|---------|----------|
| **Main Input** | Source footage to process | ✅ Yes |
| **Mask Paint/Magic Mask** | External mask for Recovery Brush | Only for paint features |

> **💡 Learning Tip**: Start with default settings and adjust **temporalThreshold** and **dirtSize** first. Use **View Mode** to switch to Difference view to see exactly what's being detected and changed.

---

## ⚙️ Requirements

- **DaVinci Resolve 18+** (Studio recommended)
- **ResolveFX Plugins**: VideoRestoration, Deflicker, NoiseReduction
- **Adequate GPU memory** for complex processing chain
- **Fusion page access** for full feature set (especially Recovery Brush)

> **💡 Note**: Recovery Brush and Spanish Dirt Removal features require the **Fusion page** and a **Mask Paint tool connection**. Edit/Cut pages support basic automatic dirt removal only.

---


---

## 🤝 Community & Support

- **Issues**: [GitHub Issues](https://github.com/fabiocolor/Dirt-Removal-Recovery/issues)
- **Discussions**: [GitHub Discussions](https://github.com/fabiocolor/Dirt-Removal-Recovery/discussions)

---

## 📄 License

This project is licensed under the MIT License - see the [LICENSE](LICENSE) file for details.

---

<div align="center">
  <sub>Making film restoration accessible to archives, educators, and preservation enthusiasts</sub>
</div>
