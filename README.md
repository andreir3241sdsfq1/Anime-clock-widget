<p align="center">
  <img src="Images/1.png" width="60"/>
  <img src="Images/3.png" width="60"/>
  <img src="Images/3.png" width="60"/>
  <img src="Images/7.png" width="60"/>
</p>

# Clock Widget (C++ / WinAPI)

A lightweight desktop clock widget built with pure WinAPI and GDI+.
Instead of standard text, the clock displays time using custom PNG images of anime characters holding digit signs.

---

## Features

- Displays time using images (0–9)
- Supports 24-hour and AM/PM formats
- Transparent background (true per-pixel alpha)
- Desktop widget mode (attached to wallpaper via WorkerW trick)
- Window mode and always-on-top mode
- Click-through (pin/unpin)
- Drag and drop positioning
- System tray icon with context menu
- Background toggle (with / without transparency)
- Fade in / fade out on open and close
- Adjustable opacity
- Settings saved to `.ini` file next to `.exe`
- **Mod/Addon API** — extend the widget with `.dll` plugins

---

## Folder Structure

```
project/
 ├── main.cpp
 ├── AddonAPI.h
 ├── settings.ini
 ├── addon_log.txt           <- created automatically
 ├── Images/
 │   ├── 0.png – 9.png
 │   └── no_BG/
 │       └── 0.png – 9.png
 └── addons/                 <- place addon .dll files here
     ├── my_addon.dll
     └── my_addon/           <- addon's own folder (settings, assets)
         └── settings.ini
```

---

## Build Instructions

Using MSVC (Developer Command Prompt):

```
cl main.cpp resource.res /EHsc /DUNICODE /D_UNICODE user32.lib gdi32.lib gdiplus.lib shell32.lib comctl32.lib
```

---

## Controls

**Mouse:**
- Left click + drag — move widget
- Right click — open menu

**Tray icon:**
- Right click — open menu

---

## Menu Options

- Desktop mode — attach to wallpaper layer
- Window mode — normal floating window
- Topmost — always on top of everything
- Pin — enable click-through (mouse passes through widget)
- Unpin — disable click-through
- Hide/Show background — switch between image sets
- AM/PM format — toggle 12h / 24h
- Addons — manage installed addons
- Settings — opacity and fade duration sliders
- Exit — fade out and close

---

## Settings Dialog

Right-click → **Settings...**

| Option | Range | Description |
|---|---|---|
| Opacity | 10 – 255 | Widget transparency |
| Fade in | 100 – 5000 ms | Duration of appear animation |
| Fade out | 100 – 5000 ms | Duration of close animation |

---

## Configuration (settings.ini)

Saved automatically next to the `.exe`.

```ini
[Settings]
x=100
y=100
mode=0       ; 0=Desktop 1=Normal 2=Topmost
click=0      ; 1=click-through enabled
bg=0         ; 1=no_BG images
ampm=0       ; 1=AM/PM format
opacity=255
fadeIn=800
fadeOut=600
```

---

## Addon / Mod API

The widget supports `.dll` addons placed in the `addons\` folder.
Addons are loaded automatically on startup and can be toggled without restarting via **Right-click → Addons**.

### Required exports

Every addon must export these four functions:

```cpp
extern "C" {
    __declspec(dllexport) const char* AddonName();
    __declspec(dllexport) const char* AddonVersion();
    __declspec(dllexport) void AddonInit(ClockAPI* api);
    __declspec(dllexport) void AddonShutdown();
}
```

### ClockAPI

Include `AddonAPI.h` from the repository root.

```cpp
struct ClockAPI {
    int apiVersion;          // = 1
    const ClockTime* time;   // current time (updated every second)
    HWND hwnd;               // main widget window handle

    // Register callbacks
    void (*OnTick)        (CB_Tick cb);         // called ~60 fps, dt in seconds
    void (*OnRender)      (CB_Render cb);       // draw on top of widget (GDI+)
    void (*OnDigitChange) (CB_DigitChange cb);  // fires when any digit changes
    void (*OnMinute)      (CB_Minute cb);       // fires every minute

    // Context menu
    void (*AddMenuItem)(const wchar_t* label, CB_MenuItem cb);

    // Utilities
    Gdiplus::Image* (*LoadImage)  (const wchar_t* addonName, const wchar_t* relativePath);
    void            (*Log)        (const wchar_t* addonName, const wchar_t* message);
    void            (*SaveSetting)(const wchar_t* addonName, const wchar_t* key, const wchar_t* value);
    const wchar_t*  (*LoadSetting)(const wchar_t* addonName, const wchar_t* key, const wchar_t* defaultVal);
    const wchar_t*  (*GetAddonDir)(const wchar_t* addonName);
    void            (*Redraw)     ();
};
```

### Callback types

```cpp
typedef void (*CB_Tick)        (float dt);
typedef void (*CB_Render)      (Gdiplus::Graphics* g, int w, int h);
typedef void (*CB_DigitChange) (int digitIdx, int oldVal, int newVal);
typedef void (*CB_Minute)      ();
typedef void (*CB_MenuItem)    ();
```

### ClockTime

```cpp
struct ClockTime {
    int  hour;    // 0-23 (always 24h internally)
    int  minute;
    int  second;
    bool isAMPM;  // user's format preference
};
```

### Minimal addon example

```cpp
#include <windows.h>
#include <gdiplus.h>
#include "AddonAPI.h"

static ClockAPI* api;

void OnTick(float dt) {
    // called every frame — update animation state here
}

void OnRender(Gdiplus::Graphics* g, int w, int h) {
    // draw anything on top of the widget using GDI+
    Gdiplus::SolidBrush br(Gdiplus::Color(120, 255, 0, 128));
    g->FillRectangle(&br, 0, 110, w, 10);
}

void OnDigitChange(int idx, int oldVal, int newVal) {
    // idx: 0=H1  1=H2  2=M1  3=M2
}

extern "C" {
    __declspec(dllexport) const char* AddonName()    { return "My Addon"; }
    __declspec(dllexport) const char* AddonVersion() { return "1.0"; }

    __declspec(dllexport) void AddonInit(ClockAPI* a) {
        api = a;
        api->OnTick(OnTick);
        api->OnRender(OnRender);
        api->OnDigitChange(OnDigitChange);
        api->AddMenuItem(L"Say hello", []{ MessageBoxW(NULL, L"Hello!", L"Addon", MB_OK); });
        api->Log(L"My Addon", L"Loaded!");
    }

    __declspec(dllexport) void AddonShutdown() {
        api->Log(L"My Addon", L"Shutdown");
    }
}
```

### Building an addon

```
cl my_addon.cpp /EHsc /DUNICODE /D_UNICODE /LD /Fe:my_addon.dll gdiplus.lib /I"path\to\repo"
```

Place the resulting `my_addon.dll` into the `addons\` folder next to the `.exe`.
Optionally create an `addons\my_addon\` folder for your own assets and settings.

### Addon settings persistence

```cpp
// Save
api->SaveSetting(L"My Addon", L"speed", L"2.5");

// Load (with default value)
const wchar_t* val = api->LoadSetting(L"My Addon", L"speed", L"2.0");
```

Saved to `addons\My Addon\settings.ini` automatically.

### Logging

```cpp
api->Log(L"My Addon", L"something happened");
```

Appended to `addon_log.txt` next to the `.exe`.

---

## Notes

- True fullscreen apps (exclusive mode) cannot be overlaid
- Windowed fullscreen works correctly
- All images must be the same size (recommended: 60x80 px)
- Addons run in the same process — a crashing addon will crash the widget
- Check `ClockAPI::apiVersion` in `AddonInit` if you need version compatibility

---

## License

Free to use and modify.
