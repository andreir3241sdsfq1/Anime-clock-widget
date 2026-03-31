<p align="center">
  <img src="Images/1.png" width="60"/>
  <img src="Images/3.png" width="60"/>
  <img src="Images/3.png" width="60"/>
  <img src="Images/7.png" width="60"/>
</p>

# Clock Widget (C++ / WinAPI)

A lightweight desktop clock widget built with pure WinAPI and GDI+.
Instead of standard text, the clock displays time using custom PNG images.

## Features

* Displays time using images (0–9)
* Supports 24-hour and AM/PM formats
* Transparent background (true per-pixel alpha)
* Desktop widget mode (attached to wallpaper)
* Window mode and always-on-top mode
* Click-through (pin/unpin)
* Drag and drop positioning
* Tray integration
* Right-click context menu
* Background toggle (with / without transparency)
* Settings saved to `.ini` file

## Folder Structure

```
project/
 ├── main.cpp
 ├── settings.ini
 └── Images/
     ├── 0.png
     ├── 1.png
     ├── ...
     ├── 9.png
     └── no_BG/
         ├── 0.png
         ├── ...
```

Optional:

```
Images/
 ├── am.png
 └── pm.png
```

## Build Instructions

Using MSVC (Developer Command Prompt):

```
cl main.cpp /EHsc /DUNICODE /D_UNICODE user32.lib gdi32.lib gdiplus.lib shell32.lib
```

## Controls

Mouse:

* Left click + drag → move widget
* Right click → open menu

Tray:

* Right click → open menu

## Menu Options

* Desktop mode → attach to wallpaper
* Window mode → normal window
* Topmost → always on top
* Pin → click-through (ignore mouse)
* Unpin → interactive mode
* Hide/Show background → switch image set
* AM/PM format → toggle time format
* Exit → close application

## Configuration (settings.ini)

The application automatically saves:

* Window position (x, y)
* Display mode
* Click-through state
* Background mode
* Time format

Example:

```
[Settings]
x=100
y=100
mode=0
click=0
bg=0
ampm=0
```

## Notes

* True fullscreen applications (exclusive mode) cannot be overlaid
* Windowed fullscreen works correctly
* All images must have the same size for best results (recommended: 60x80)

## Possible Improvements

* Smooth digit animations
* Theme system (multiple image packs)
* Character/mascot integration
* Auto-hide in fullscreen apps
* Snap-to-screen edges
* Startup with Windows

## License

Free to use and modify.
