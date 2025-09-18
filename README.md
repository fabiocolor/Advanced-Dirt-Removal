# Advanced Dirt Removal for DaVinci Resolve

[![Version](https://img.shields.io/badge/version-v1.0-blue?style=flat-square)](https://github.com/fabiocolor/Dirt-Removal-Recovery/releases)
[![License](https://img.shields.io/badge/license-MIT-green?style=flat-square)](LICENSE)
[![DaVinci Resolve](https://img.shields.io/badge/DaVinci%20Resolve-18%2B-red?style=flat-square)](https://www.blackmagicdesign.com/products/davinciresolve)
[![Platform](https://img.shields.io/badge/platform-macOS%20%7C%20Windows%20%7C%20Linux-lightgrey?style=flat-square)](#installation)

[![Download](https://img.shields.io/badge/Download-Latest%20Release-brightgreen?style=for-the-badge&logo=download)](https://github.com/fabiocolor/Dirt-Removal-Recovery/releases/latest)

---

**Advanced Dirt Removal** is a professional Fusion macro for DaVinci Resolve that revolutionizes film restoration workflows. It extends the built-in Automatic Dirt Removal with intelligent motion-masking, manual recovery tools, and real-time diagnostic views.

<!-- Screenshot placeholder: before-after-hero.jpg -->
<!-- *Professional dirt removal results on archival footage* -->

> 🎯 **Perfect for**: Film restoration, archival work, cleaning vintage footage, and preserving historical content

---

## ✨ Key Features

**🔄 Dual Recovery Modes**
- **Motion Mask**: Automatically restricts repairs to low/no-motion areas
- **Recovery Brush**: Manual painting to restore original details

**🎨 Smart Processing Pipeline**
- Advanced optical flow analysis
- Temporal noise reduction and deflicker
- ResolveFX VideoRestoration engine integration

**👁️ Real-time Diagnostics**
- **Difference View**: Live comparison between original and processed
- **Spanish Dirt Removal**: Toggle brush polarity (protect vs repair)
- **Hybrid Workflow**: Combine automatic and manual techniques

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

<!-- Screenshot placeholder: fusion-node-graph.jpg -->
<!-- *Clean integration in Fusion workflow* -->

1. **Add the Effect**
   - **Fusion Page**: `Add Tool > Macros > Advanced Dirt Removal`
   - **Edit/Cut Pages**: `Effects > Templates > Advanced Dirt Removal`

2. **Connect Your Source**
   - Drag from MediaIn to Advanced Dirt Removal input

3. **Choose Recovery Mode**
   - **Motion Mask**: Automatic motion-based protection
   - **Recovery Brush**: Manual painting control (requires mask connection - see below)

<!-- Screenshot placeholder: inspector-controls.jpg -->
<!-- *Intuitive parameter interface* -->

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
  <sub>Built with ❤️ for the film restoration community</sub>
</div>
