Fuse Development Kit.


* Fuses are like plugin addon tools that run in Resolve, Resolve Studio and Fusion Studio on all OSes.
* Active community with many free Fuse plugins, 50+ already available
* Resolve and Fusion have a built in Compiler that will real time compile and execute Fuse code to process images.
* No Compilers or Development Environments needed, just a text editor.
* No restarting of Resolve or Fusion. One click will recompile and run the code.
* Development is done via the Fusion page, use Templates method to publish in Resolve.
* Fuses have a lightweight SDK-like feature set, using the feature rich Lua language
* Fuse DK has a tool kit of built in functions to rapidly develop tool nodes.
   * UI: Library of sliders, color controls, text, buttons, on screen widgets (requires Resolve Studio or Fusion Studio)
   * Image Processing Library: Core of the Fusion engine’s highly optimized functions set.
   * Image channels. All Auxiliary channels can be processed. Boolean and Math functions can be applied
   * Shapes Library: LInes, Bezier curves, filled shapes, colors, and styles 
   * Text: Input box, Font choice, colors, sizes and transformation toolset
   * Pixel sampling and non linear spatial operations 
   * Metadata functions
   * Viewer LUT plugins using GPU and GLSL shaders
   * DCTL GPU processing


Installation.


Copy all the Fuse plugins to one of these directories. Restart Resolve or Fusion.


Resolve
* macOS:   ~/Library/Application Support/Blackmagic Design/DaVinci Resolve/Fusion/Fuses
* Windows: C:\ProgramData\Blackmagic Design\DaVinci Resolve\Support\Fusion\Fuses
* Linux:   /.local/share/DaVinciResolve/Fusion/Fuses

Fusion
* macOS:   ~/Library/Application Support/Blackmagic Design/Fusion/Fuses
* Windows: C:\ProgramData\Blackmagic Design\Fusion\Fuses
* Linux:   ~/.fusion/BlackmagicDesign/Fusion/Fuses


Note that fuses are loaded from folders in the Fuses: path map. These folders may be
viewed or edited in the Fusion Settings dialog in Resolve, or the Preferences dialog in
Fusion Studio, under Path Maps. Folders may be shown via tool tip, or opened with the
right-click menu.


Examples.


* Load up the Examples.comp in Fusion, or the Fuse.prj in Resolve.
* Edit the Fuse code and Reload to see immediate results in the Fusion page.
* Read the manual. 
* Tutorial Guide for Example Fuses 1 to 7
* The examples are outlined in the manual and have inline comments.
* LightWrap Merge and Blur Merge are usable tools.

