<p align="center">
  <img src="Images/1.png" width="60"/>
  <img src="Images/3.png" width="60"/>
  <img src="Images/3.png" width="60"/>
  <img src="Images/7.png" width="60"/>
</p>
Clock Widget (C++ / WinAPI)
A lightweight desktop clock widget built with pure WinAPI and GDI+.
Instead of standard text, the clock displays time using custom PNG images — anime characters holding number signs.
Features
Displays time using images (0–9)
Supports 24-hour and AM/PM formats
Transparent background (true per-pixel alpha)
Desktop widget mode (attached to wallpaper via WorkerW trick)
Window mode and always-on-top mode
Click-through (pin/unpin)
Drag and drop positioning
Fade in / fade out on launch and exit
Tray icon (system tray, near the clock)
Right-click context menu
Background toggle (with / without transparency)
Configurable opacity and fade duration (Settings dialog)
Settings saved to `.ini` file
Addon system — extend the widget with `.dll` plugins
---
Folder Structure
```
project/
├── main.cpp
├── AddonAPI.h
├── settings.ini
├── addon_log.txt          ← created automatically
├── Images/
│   ├── 0.png ... 9.png
│   └── no_BG/
│       ├── 0.png ... 9.png
└── addons/
    ├── my_addon.dll
    └── my_addon/          ← addon's own folder (settings, assets)
        └── settings.ini
```
---
Build Instructions
Using MSVC (Developer Command Prompt):
```
cl main.cpp resource.res /EHsc /DUNICODE /D_UNICODE user32.lib gdi32.lib gdiplus.lib shell32.lib comctl32.lib
```
---
Controls
Mouse:
Left click + drag → move widget
Right click → open menu
Tray icon:
Right click → open menu
---
Menu Options
Option	Description
Desktop mode	Attach widget to wallpaper (behind icons)
Window mode	Normal floating window
Topmost	Always on top of all windows
Pin / Unpin	Toggle click-through mode
Hide / Show background	Switch between image sets
AM/PM format	Toggle 12h / 24h
Addons...	Open addon manager
Settings...	Opacity and fade duration sliders
Exit	Fade out and close
---
Settings Dialog
Accessible via right-click → Settings...
Setting	Range	Description
Opacity	10–255	Widget transparency
Fade in	100–5000 ms	Duration of appear animation
Fade out	100–5000 ms	Duration of exit animation
---
Configuration (settings.ini)
Saved automatically on move, menu change, or drag release.
```ini
[Settings]
x=100
y=100
mode=0
click=0
bg=0
ampm=0
opacity=255
fadeIn=800
fadeOut=600
```
---
Addon System
Addons are `.dll` files placed in the `addons\` folder next to the `.exe`.
They are loaded automatically on startup.
Addons can be toggled on/off at runtime via right-click → Addons.
Addon Manager
```
┌──────────────────────────────────┐
│ Addons                           │
├──────────────────────────────────┤
│ [X] Bounce Animation   v1.0      │
│ [X] Rainbow Overlay    v0.3      │
│ [ ] Shake on Hour      v1.2      │
│                                  │
│  Click to toggle on/off          │
│              [OK]                │
└──────────────────────────────────┘
```
Disabling unloads the `.dll` from memory. Enabling reloads it.
---
Creating an Addon
Include `AddonAPI.h` and export these four functions:
```cpp
#include "AddonAPI.h"

extern "C" {
    __declspec(dllexport) const char* AddonName()    { return "My Addon"; }
    __declspec(dllexport) const char* AddonVersion() { return "1.0"; }
    __declspec(dllexport) void AddonInit(ClockAPI* api) { /* setup here */ }
    __declspec(dllexport) void AddonShutdown()          { /* cleanup here */ }
}
```
Compile as a DLL:
```
cl my_addon.cpp /EHsc /DUNICODE /D_UNICODE /LD /Fe:my_addon.dll gdiplus.lib /I"path\to\AddonAPI.h"
```
Place `my_addon.dll` in the `addons\` folder.
---
AddonAPI Reference
The `ClockAPI*` pointer is passed to `AddonInit`. Use it to register callbacks and access utilities.
Time
```cpp
const ClockTime* api->time;

struct ClockTime {
    int  hour;    // 0–23 (always 24h internally)
    int  minute;
    int  second;
    bool isAMPM;  // user's format setting
};
```
Callbacks
```cpp
// Called every ~16ms (~60fps). Use for animations.
api->OnTick([](float dt) {
    g_phase += dt * 2.0f;
});

// Called after the widget renders. Draw on top using GDI+.
api->OnRender([](Gdiplus::Graphics* g, int w, int h) {
    // draw anything over the widget
});

// Called when any of the 4 displayed digits changes.
// digitIdx: 0=H1, 1=H2, 2=M1, 3=M2
api->OnDigitChange([](int digitIdx, int oldVal, int newVal) {
    // e.g. trigger a jump animation
});

// Called every minute.
api->OnMinute([]() {
    // e.g. play a sound or flash effect
});
```
Context Menu
```cpp
// Adds items to Addons → <AddonName> submenu
api->AddMenuItem(L"Speed: Fast",   []() { g_speed = 4.0f; });
api->AddMenuItem(L"Speed: Normal", []() { g_speed = 2.0f; });
```
Images
```cpp
// Load an image from the addon's own folder (addons\MyAddon\sprite.png)
Gdiplus::Image* img = api->LoadImage(L"MyAddon", L"sprite.png");
```
Settings
```cpp
// Saved to addons\MyAddon\settings.ini
api->SaveSetting(L"MyAddon", L"speed", L"2.0");
const wchar_t* val = api->LoadSetting(L"MyAddon", L"speed", L"2.0");
```
Utilities
```cpp
const wchar_t* dir = api->GetAddonDir(L"MyAddon");
// → "C:\path\to\widget\addons\MyAddon\"

api->Log(L"MyAddon", L"initialized");
// → appended to addon_log.txt

api->Redraw();
// → forces an immediate re-render of the widget
```
HWND
```cpp
HWND hwnd = api->hwnd; // main widget window
```
---
Example Addon — Bounce Animation
Full source in `addons_example/bounce_anim.cpp`.
```cpp
#include "AddonAPI.h"
#include <cmath>

static float g_time  = 0.0f;
static float g_speed = 2.0f;

void OnTick(float dt)   { g_time += dt; }

void OnRender(Gdiplus::Graphics* g, int w, int h) {
    // Animate each of the 4 digit positions
    const float phase[4] = { 0.0f, 1.1f, 2.2f, 3.3f };
    Gdiplus::SolidBrush br(Gdiplus::Color(40, 100, 180, 255));
    int xpos[4] = { 10, 75, 160, 225 };
    for (int i = 0; i < 4; i++) {
        float offset = sinf(g_time * g_speed + phase[i]) * 6.0f;
        g->FillEllipse(&br, xpos[i]+20, (int)(100 + offset*0.3f), 20, 6);
    }
}

extern "C" {
    __declspec(dllexport) const char* AddonName()    { return "Bounce Animation"; }
    __declspec(dllexport) const char* AddonVersion() { return "1.0"; }
    __declspec(dllexport) void AddonInit(ClockAPI* api) {
        api->OnTick(OnTick);
        api->OnRender(OnRender);
        api->AddMenuItem(L"Speed: Fast",   []() { g_speed = 4.0f; });
        api->AddMenuItem(L"Speed: Normal", []() { g_speed = 2.0f; });
        api->AddMenuItem(L"Speed: Slow",   []() { g_speed = 0.8f; });
        api->Log(L"Bounce Animation", L"Loaded");
    }
    __declspec(dllexport) void AddonShutdown() {}
}
```
---
Notes
True fullscreen (exclusive) apps cannot be overlaid — use windowed fullscreen
All images should be the same size (recommended: 60×80 px)
Addon `.dll` files must target the same architecture as the main exe (x86 or x64)
`addon_log.txt` is appended on each run — clear it manually if needed
---
License
Free to use and modify.
