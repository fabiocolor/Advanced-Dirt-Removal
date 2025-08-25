# Advanced Dirt Removal for DaVinci Resolve

This project provides "Advanced Dirt Removal", a Fusion macro for DaVinci Resolve that extends the built-in Automatic Dirt Removal with motion-masking, a manual Recovery Brush, and a paint-driven "Spanish Dirt Removal" mode. It lets you restrict automatic repairs to low- or no-motion areas, paint to recover original source detail where the automatic pass removed wanted information, and preview a realtime difference view so you can iterate quickly during film-restoration workflows.

## Installation

1.  **Download the `Advanced Dirt Removal.setting` file** from the latest [release](https://github.com/octimot/Advanced-Dirt-Removal-Recovery/releases) or the root of this repository.
2.  **Copy the `.setting` file** to the DaVinci Resolve Effects folder on your system:
    *   **macOS:** `~/Library/Application Support/Blackmagic Design/DaVinci Resolve/Fusion/Templates/Edit/Effects`
    *   **Windows:** `%APPDATA%\Blackmagic Design\DaVinci Resolve\Support\Fusion\Templates\Edit\Effects`
    *   **Linux:** `~/.local/share/DaVinciResolve/Fusion/Templates/Edit/Effects` (path may vary)

## Usage

Once installed, the "Advanced Dirt Removal" effect is available in DaVinci Resolve:

1.  Open DaVinci Resolve and go to the **Fusion** page (recommended for full feature access). You can also use the **Edit**, **Cut**, or **Color** pages if you only need the automatic dirt removal and do not plan to use the Recovery Brush.
2.  On Edit/Cut pages, open the **Effects** library. In Fusion, add the macro using the Add Tool menu or the Tools browser: Add Tool > Macros > Advanced Dirt Removal. You can also open the Tools browser and navigate to Toolbox > Macros > Advanced Dirt Removal.
3.  On Edit/Cut pages the macro appears under **Toolbox > Effects > Templates**; in Fusion it appears under **Toolbox > Macros**.
4.  Drag the effect onto a clip in your timeline (Edit/Cut) or add it to your Fusion composition (right-click in the node area or use Add Tool > Macros).
5.  Use the **Inspector** (Edit/Cut) or the macro controls in Fusion to adjust parameters.

## Features

This macro augments Resolve's built-in Automatic Dirt Removal by combining an automatic repair pass with flexible masking and manual recovery tools.

- Recovery modes: switch between "Recovery Brush" and "Motion Mask".
- Motion-restricted auto repair: the Motion Mask mode limits the automatic dirt removal to low- or no-motion areas so moving content is preserved while stationary dirt is repaired.
- Manual recovery brush: the "Recovery Brush" mode lets you paint a mask to bring original source details back where the automatic repair removed wanted features.
- Hybrid workflow: even when using the Motion Mask you can complement it with manual paints to tweak which areas are repaired or recovered.
 - "Spanish Dirt Removal" toggle (`Recovery/Spanish Dirt Removal` → maps to `ApplyMaskInverted`): controls the brush polarity and changes how painted pixels are interpreted by the automatic repair.
     - Recovery polarity (paint protects source): painted areas act as a recovery mask — the macro will favour the original Source in painted regions and prevent the automatic dirt removal from replacing those details.
     - Spanish (paint-to-repair) polarity: painted areas instead tell the automatic dirt-removal engine where to apply the repair at full strength — effectively you are painting the repair rather than painting a protection mask.
     - Practical notes: this toggle works with the published `Mask Paint/Magic Mask` input and applies in both Recovery Brush and Motion Mask workflows. Use the `Recovery type` switch to choose between Recovery Brush and Motion Mask, then paint to protect or to target repairs depending on the Spanish Dirt Removal setting. Use the Difference View to verify paint results in real time.
- Difference view: a realtime comparison mode shows the difference between the source and processed output so you can quickly see what has changed and decide what to recover or mask further.

Note: For full functionality use the macro on the **Fusion** page. The Edit and Color pages support basic automatic dirt removal but do not expose the Fusion paint input; however, you can still use Magic Mask or other node-based masks as alternatives on those pages.

## Technical Specifications

For more detailed information on the tool's inner workings, see the [Technical Specifications](docs/TECH_SPEC.md).
