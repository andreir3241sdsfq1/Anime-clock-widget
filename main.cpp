#include <windows.h>
#include <shellapi.h>
#include <gdiplus.h>
#include <string>
#include <vector>
#include <ctime>

#pragma comment (lib,"Gdiplus.lib")

using namespace Gdiplus;

#define WM_TRAYICON (WM_USER + 1)

ULONG_PTR gdiplusToken;
std::vector<Image*> digits(10);

// настройки
bool useNoBG = false;
bool clickThrough = false;
bool useAMPM = false;

enum Mode { DESKTOP, NORMAL, TOPMOST };
Mode currentMode = DESKTOP;

// drag
bool dragging = false;
POINT dragStart, windowStart;

// ini путь
const wchar_t* iniFile = L"settings.ini";

// 📥 загрузка
void LoadImages()
{
    for (int i = 0; i < 10; i++)
    {
        if (digits[i]) delete digits[i];

        std::wstring base = useNoBG ? L"Images\\no_BG\\" : L"Images\\";
        std::wstring path = base + std::to_wstring(i) + L".png";

        digits[i] = Image::FromFile(path.c_str());
    }
}

// 💾 сохранить
void SaveSettings(HWND hwnd)
{
    RECT r;
    GetWindowRect(hwnd, &r);

    WritePrivateProfileString(L"Settings", L"x", std::to_wstring(r.left).c_str(), iniFile);
    WritePrivateProfileString(L"Settings", L"y", std::to_wstring(r.top).c_str(), iniFile);
    WritePrivateProfileString(L"Settings", L"mode", std::to_wstring(currentMode).c_str(), iniFile);
    WritePrivateProfileString(L"Settings", L"click", clickThrough ? L"1" : L"0", iniFile);
    WritePrivateProfileString(L"Settings", L"bg", useNoBG ? L"1" : L"0", iniFile);
    WritePrivateProfileString(L"Settings", L"ampm", useAMPM ? L"1" : L"0", iniFile);
}

// 📂 загрузить
void LoadSettings(int& x, int& y)
{
    x = GetPrivateProfileInt(L"Settings", L"x", 100, iniFile);
    y = GetPrivateProfileInt(L"Settings", L"y", 100, iniFile);

    currentMode = (Mode)GetPrivateProfileInt(L"Settings", L"mode", 0, iniFile);
    clickThrough = GetPrivateProfileInt(L"Settings", L"click", 0, iniFile);
    useNoBG = GetPrivateProfileInt(L"Settings", L"bg", 0, iniFile);
    useAMPM = GetPrivateProfileInt(L"Settings", L"ampm", 0, iniFile);
}

// 🕒 рисуем время
void DrawTime(Graphics& g)
{
    time_t t = time(0);
    tm now;
    localtime_s(&now, &t);

    int hour = now.tm_hour;

    if (useAMPM)
    {
        hour = hour % 12;
        if (hour == 0) hour = 12;
    }

    int h1 = hour / 10;
    int h2 = hour % 10;
    int m1 = now.tm_min / 10;
    int m2 = now.tm_min % 10;

    int x = 10;

    g.DrawImage(digits[h1], x, 10, 60, 80); x += 65;
    g.DrawImage(digits[h2], x, 10, 60, 80); x += 65;
    x += 20;
    g.DrawImage(digits[m1], x, 10, 60, 80); x += 65;
    g.DrawImage(digits[m2], x, 10, 60, 80);
}

// 🎨 рендер
void Render(HWND hwnd)
{
    HDC screenDC = GetDC(NULL);
    HDC memDC = CreateCompatibleDC(screenDC);

    int width = 300, height = 120;

    BITMAPINFO bmi = {};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = width;
    bmi.bmiHeader.biHeight = -height;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;

    void* bits;
    HBITMAP bmp = CreateDIBSection(screenDC, &bmi, DIB_RGB_COLORS, &bits, NULL, 0);
    SelectObject(memDC, bmp);

    Graphics g(memDC);
    g.Clear(Color(0, 0, 0, 0));

    DrawTime(g);

    RECT r;
    GetWindowRect(hwnd, &r);

    POINT ptPos = { r.left, r.top };
    POINT ptSrc = { 0,0 };
    SIZE size = { width, height };

    BLENDFUNCTION blend = { AC_SRC_OVER,0,255,AC_SRC_ALPHA };

    UpdateLayeredWindow(hwnd, screenDC, &ptPos, &size, memDC, &ptSrc, 0, &blend, ULW_ALPHA);

    DeleteObject(bmp);
    DeleteDC(memDC);
    ReleaseDC(NULL, screenDC);
}

// 🧱 desktop
void AttachToDesktop(HWND hwnd)
{
    HWND progman = FindWindow(L"Progman", NULL);
    SendMessage(progman, 0x052C, 0, 0);

    HWND workerw = NULL;

    EnumWindows([](HWND top, LPARAM param)->BOOL
    {
        HWND shell = FindWindowEx(top, NULL, L"SHELLDLL_DefView", NULL);
        if (shell)
        {
            HWND* ret = (HWND*)param;
            *ret = FindWindowEx(NULL, top, L"WorkerW", NULL);
        }
        return TRUE;
    }, (LPARAM)&workerw);

    if (workerw) SetParent(hwnd, workerw);
}

// ⚙️ режим
void ApplyMode(HWND hwnd)
{
    if (currentMode == DESKTOP) AttachToDesktop(hwnd);
    else if (currentMode == NORMAL)
    {
        SetParent(hwnd, NULL);
        SetWindowPos(hwnd, HWND_NOTOPMOST, 0,0,0,0, SWP_NOMOVE|SWP_NOSIZE);
    }
    else
    {
        SetParent(hwnd, NULL);
        SetWindowPos(hwnd, HWND_TOPMOST, 0,0,0,0, SWP_NOMOVE|SWP_NOSIZE);
    }
}

// 🖱 click-through
void UpdateClickMode(HWND hwnd)
{
    LONG ex = GetWindowLong(hwnd, GWL_EXSTYLE);

    if (clickThrough) SetWindowLong(hwnd, GWL_EXSTYLE, ex | WS_EX_TRANSPARENT);
    else SetWindowLong(hwnd, GWL_EXSTYLE, ex & ~WS_EX_TRANSPARENT);
}

// 🪟 окно
LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
    case WM_CREATE:
        SetTimer(hwnd, 1, 1000, NULL);
        break;

    case WM_TIMER:
        Render(hwnd);
        break;

    case WM_LBUTTONDOWN:
        dragging = true;
        GetCursorPos(&dragStart);
        {
            RECT r;
            GetWindowRect(hwnd, &r);
            windowStart = { r.left, r.top };
        }
        SetCapture(hwnd);
        break;

    case WM_MOUSEMOVE:
        if (dragging)
        {
            POINT cur;
            GetCursorPos(&cur);

            SetWindowPos(hwnd, NULL,
                windowStart.x + (cur.x - dragStart.x),
                windowStart.y + (cur.y - dragStart.y),
                0,0, SWP_NOSIZE | SWP_NOZORDER);

            Render(hwnd);
        }
        break;

    case WM_LBUTTONUP:
        dragging = false;
        ReleaseCapture();
        SaveSettings(hwnd);
        break;

    case WM_RBUTTONUP:
    {
        HMENU menu = CreatePopupMenu();

        AppendMenu(menu, MF_STRING, 1, L"Desktop mode");
        AppendMenu(menu, MF_STRING, 2, L"Window mode");
        AppendMenu(menu, MF_STRING, 3, L"Topmost");

        AppendMenu(menu, MF_SEPARATOR, 0, NULL);
        AppendMenu(menu, MF_STRING, 5, clickThrough ? L"Unpin" : L"Pin");
        AppendMenu(menu, MF_STRING, 6, useNoBG ? L"Show background" : L"Hide background");
        AppendMenu(menu, MF_STRING, 7, useAMPM ? L"24h format" : L"AM/PM format");

        AppendMenu(menu, MF_SEPARATOR, 0, NULL);
        AppendMenu(menu, MF_STRING, 4, L"Exit");

        POINT pt;
        GetCursorPos(&pt);

        int cmd = TrackPopupMenu(menu, TPM_RETURNCMD, pt.x, pt.y, 0, hwnd, NULL);

        if (cmd == 1) currentMode = DESKTOP;
        if (cmd == 2) currentMode = NORMAL;
        if (cmd == 3) currentMode = TOPMOST;
        if (cmd == 4) PostQuitMessage(0);

        if (cmd == 5) clickThrough = !clickThrough;
        if (cmd == 6) { useNoBG = !useNoBG; LoadImages(); }
        if (cmd == 7) useAMPM = !useAMPM;

        ApplyMode(hwnd);
        UpdateClickMode(hwnd);
        SaveSettings(hwnd);

        DestroyMenu(menu);
        break;
    }

    case WM_DESTROY:
        PostQuitMessage(0);
        break;
    }

    return DefWindowProc(hwnd, msg, wParam, lParam);
}

// 🚀 старт
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int)
{
    GdiplusStartupInput gdiplusStartupInput;
    GdiplusStartup(&gdiplusToken, &gdiplusStartupInput, NULL);

    int x, y;
    LoadSettings(x, y);

    LoadImages();

    WNDCLASS wc = {};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = L"ClockWidget";

    RegisterClass(&wc);

    HWND hwnd = CreateWindowEx(
        WS_EX_LAYERED,
        wc.lpszClassName,
        L"",
        WS_POPUP,
        x, y, 300, 120,
        NULL, NULL, hInstance, NULL
    );

    ShowWindow(hwnd, SW_SHOW);

    ApplyMode(hwnd);
    UpdateClickMode(hwnd);

    Render(hwnd);

    MSG msg = {};
    while (GetMessage(&msg, NULL, 0, 0))
    {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    GdiplusShutdown(gdiplusToken);
    return 0;
}