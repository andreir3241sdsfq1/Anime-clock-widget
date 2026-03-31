#include <windows.h>
#include <shellapi.h>
#include <gdiplus.h>
#include <string>
#include <vector>
#include <ctime>
#include <commctrl.h>
#include <algorithm>
#include "AddonAPI.h"

#pragma comment(lib, "Gdiplus.lib")
#pragma comment(lib, "Comctl32.lib")

using namespace Gdiplus;

// ============================================================
// КОНСТАНТЫ
// ============================================================
#define IDT_CLOCK       1
#define IDT_FADE        2
#define IDT_ANIM        3
#define WM_TRAYICON     (WM_USER + 1)
#define TRAY_ID         1

// ============================================================
// ГЛОБАЛЬНЫЕ ПЕРЕМЕННЫЕ
// ============================================================
ULONG_PTR  gdiplusToken;
HINSTANCE  g_hInst;
HWND       g_hwnd;
NOTIFYICONDATA g_nid = {};

std::vector<Image*> digits(10);

// Настройки
bool useNoBG      = false;
bool clickThrough = false;
bool useAMPM      = false;
int  opacity      = 255;
int  fadeInMs     = 800;
int  fadeOutMs    = 600;

enum Mode { DESKTOP, NORMAL, TOPMOST };
Mode currentMode = DESKTOP;

// Drag
bool  dragging = false;
POINT dragStart, windowStart;

// Fade
enum FadeState { FADE_NONE, FADE_IN, FADE_OUT };
FadeState fadeState     = FADE_NONE;
int       fadeCurrent   = 0;
DWORD     fadeStep      = 16;
bool      exitAfterFade = false;

// Время (текущее)
ClockTime g_time = {};
int g_prevDigits[4] = {-1,-1,-1,-1};

// Пути
wchar_t g_exeDir[MAX_PATH];
wchar_t g_iniFile[MAX_PATH];

// ============================================================
// ADDON SYSTEM
// ============================================================
struct AddonEntry {
    wchar_t  path[MAX_PATH];
    char     name[64];
    char     version[32];
    HMODULE  hModule;
    bool     enabled;
    FN_AddonShutdown fnShutdown;

    // Зарегистрированные колбэки
    std::vector<CB_Tick>        cbTick;
    std::vector<CB_Render>      cbRender;
    std::vector<CB_DigitChange> cbDigitChange;
    std::vector<CB_Minute>      cbMinute;

    // Пункты меню аддона
    struct MenuItem { std::wstring label; CB_MenuItem cb; };
    std::vector<MenuItem> menuItems;
};

std::vector<AddonEntry*> g_addons;

// Буфер для возврата строк из API
wchar_t g_apiStrBuf[MAX_PATH];

// ---- Форварды ----
void Render(HWND hwnd, BYTE alpha);
void FireAddonTick(float dt);
void FireAddonRender(Graphics* g, int w, int h);

// ---- API-функции (передаются аддонам) ----

// Найти запись аддона по имени (используется внутри колбэков)
// Для регистрации колбэков используем "текущий загружаемый" аддон
static AddonEntry* g_currentLoadingAddon = nullptr;

void API_OnTick(CB_Tick cb)         { if (g_currentLoadingAddon) g_currentLoadingAddon->cbTick.push_back(cb); }
void API_OnRender(CB_Render cb)     { if (g_currentLoadingAddon) g_currentLoadingAddon->cbRender.push_back(cb); }
void API_OnDigitChange(CB_DigitChange cb) { if (g_currentLoadingAddon) g_currentLoadingAddon->cbDigitChange.push_back(cb); }
void API_OnMinute(CB_Minute cb)     { if (g_currentLoadingAddon) g_currentLoadingAddon->cbMinute.push_back(cb); }

void API_AddMenuItem(const wchar_t* label, CB_MenuItem cb) {
    if (g_currentLoadingAddon)
        g_currentLoadingAddon->menuItems.push_back({label, cb});
}

Gdiplus::Image* API_LoadImage(const wchar_t* addonName, const wchar_t* relPath) {
    wchar_t full[MAX_PATH];
    swprintf_s(full, L"%saddons\\%s\\%s", g_exeDir, addonName, relPath);
    auto img = Image::FromFile(full);
    return (img && img->GetLastStatus() == Ok) ? img : nullptr;
}

void API_Log(const wchar_t* addonName, const wchar_t* msg) {
    wchar_t logPath[MAX_PATH];
    swprintf_s(logPath, L"%saddon_log.txt", g_exeDir);
    FILE* f = nullptr;
    _wfopen_s(&f, logPath, L"a, ccs=UTF-8");
    if (f) { fwprintf(f, L"[%s] %s\n", addonName, msg); fclose(f); }
}

void API_SaveSetting(const wchar_t* addonName, const wchar_t* key, const wchar_t* val) {
    wchar_t ini[MAX_PATH];
    swprintf_s(ini, L"%saddons\\%s\\settings.ini", g_exeDir, addonName);
    WritePrivateProfileString(L"Addon", key, val, ini);
}

const wchar_t* API_LoadSetting(const wchar_t* addonName, const wchar_t* key, const wchar_t* def) {
    wchar_t ini[MAX_PATH];
    swprintf_s(ini, L"%saddons\\%s\\settings.ini", g_exeDir, addonName);
    GetPrivateProfileString(L"Addon", key, def, g_apiStrBuf, MAX_PATH, ini);
    return g_apiStrBuf;
}

const wchar_t* API_GetAddonDir(const wchar_t* addonName) {
    swprintf_s(g_apiStrBuf, L"%saddons\\%s\\", g_exeDir, addonName);
    return g_apiStrBuf;
}

void API_Redraw() { if (g_hwnd) Render(g_hwnd, (BYTE)opacity); }

// Единственный экземпляр ClockAPI (передаём указатель каждому аддону)
ClockAPI g_api = {};

void InitAPI() {
    g_api.apiVersion     = 1;
    g_api.time           = &g_time;
    g_api.hwnd           = nullptr; // заполним после создания окна
    g_api.OnTick         = API_OnTick;
    g_api.OnRender       = API_OnRender;
    g_api.OnDigitChange  = API_OnDigitChange;
    g_api.OnMinute       = API_OnMinute;
    g_api.AddMenuItem    = API_AddMenuItem;
    g_api.LoadImage      = API_LoadImage;
    g_api.Log            = API_Log;
    g_api.SaveSetting    = API_SaveSetting;
    g_api.LoadSetting    = API_LoadSetting;
    g_api.GetAddonDir    = API_GetAddonDir;
    g_api.Redraw         = API_Redraw;
}

// Загрузить один аддон
void LoadAddon(const wchar_t* dllPath)
{
    HMODULE hm = LoadLibraryW(dllPath);
    if (!hm) return;

    auto fnName    = (FN_AddonName)   GetProcAddress(hm, "AddonName");
    auto fnVersion = (FN_AddonVersion)GetProcAddress(hm, "AddonVersion");
    auto fnInit    = (FN_AddonInit)   GetProcAddress(hm, "AddonInit");
    auto fnShutdown= (FN_AddonShutdown)GetProcAddress(hm,"AddonShutdown");

    if (!fnName || !fnInit) { FreeLibrary(hm); return; }

    AddonEntry* e = new AddonEntry();
    wcscpy_s(e->path, dllPath);
    strcpy_s(e->name,    fnName    ? fnName()    : "Unknown");
    strcpy_s(e->version, fnVersion ? fnVersion() : "?");
    e->hModule    = hm;
    e->enabled    = true;
    e->fnShutdown = fnShutdown;

    g_currentLoadingAddon = e;
    fnInit(&g_api);
    g_currentLoadingAddon = nullptr;

    g_addons.push_back(e);
}

void UnloadAddon(AddonEntry* e)
{
    if (!e->hModule) return;
    if (e->fnShutdown) e->fnShutdown();
    FreeLibrary(e->hModule);
    e->hModule = nullptr;
    e->cbTick.clear();
    e->cbRender.clear();
    e->cbDigitChange.clear();
    e->cbMinute.clear();
}

void ReloadAddon(AddonEntry* e)
{
    UnloadAddon(e);

    HMODULE hm = LoadLibraryW(e->path);
    if (!hm) return;

    auto fnInit    = (FN_AddonInit)   GetProcAddress(hm, "AddonInit");
    auto fnShutdown= (FN_AddonShutdown)GetProcAddress(hm,"AddonShutdown");
    if (!fnInit) { FreeLibrary(hm); return; }

    e->hModule    = hm;
    e->fnShutdown = fnShutdown;
    e->enabled    = true;

    g_currentLoadingAddon = e;
    fnInit(&g_api);
    g_currentLoadingAddon = nullptr;
}

void ScanAndLoadAddons()
{
    wchar_t pattern[MAX_PATH];
    swprintf_s(pattern, L"%saddons\\*.dll", g_exeDir);

    WIN32_FIND_DATAW fd;
    HANDLE hf = FindFirstFileW(pattern, &fd);
    if (hf == INVALID_HANDLE_VALUE) return;

    do {
        wchar_t full[MAX_PATH];
        swprintf_s(full, L"%saddons\\%s", g_exeDir, fd.cFileName);

        // Не грузим уже загруженные
        bool already = false;
        for (auto* a : g_addons)
            if (_wcsicmp(a->path, full) == 0) { already = true; break; }
        if (!already) LoadAddon(full);

    } while (FindNextFileW(hf, &fd));
    FindClose(hf);
}

// Вызов колбэков
void FireAddonTick(float dt) {
    for (auto* a : g_addons) if (a->enabled && a->hModule)
        for (auto cb : a->cbTick) cb(dt);
}
void FireAddonRender(Graphics* g, int w, int h) {
    for (auto* a : g_addons) if (a->enabled && a->hModule)
        for (auto cb : a->cbRender) cb(g, w, h);
}
void FireAddonDigitChange(int idx, int ov, int nv) {
    for (auto* a : g_addons) if (a->enabled && a->hModule)
        for (auto cb : a->cbDigitChange) cb(idx, ov, nv);
}
void FireAddonMinute() {
    for (auto* a : g_addons) if (a->enabled && a->hModule)
        for (auto cb : a->cbMinute) cb();
}

// ============================================================
// ПУТИ
// ============================================================
void InitPaths()
{
    GetModuleFileNameW(NULL, g_exeDir, MAX_PATH);
    wchar_t* sl = wcsrchr(g_exeDir, L'\\');
    if (sl) *(sl+1) = L'\0';

    swprintf_s(g_iniFile, L"%ssettings.ini", g_exeDir);

    // Создать папку addons если нет
    wchar_t addonsDir[MAX_PATH];
    swprintf_s(addonsDir, L"%saddons", g_exeDir);
    CreateDirectoryW(addonsDir, NULL);
}

// ============================================================
// ИЗОБРАЖЕНИЯ
// ============================================================
void LoadImages()
{
    for (int i = 0; i < 10; i++) {
        if (digits[i]) { delete digits[i]; digits[i] = nullptr; }
        std::wstring base = std::wstring(g_exeDir) + (useNoBG ? L"Images\\no_BG\\" : L"Images\\");
        std::wstring path = base + std::to_wstring(i) + L".png";
        auto img = Image::FromFile(path.c_str());
        digits[i] = (img && img->GetLastStatus() == Ok) ? img : nullptr;
    }
}

// ============================================================
// НАСТРОЙКИ
// ============================================================
void SaveSettings(HWND hwnd)
{
    RECT r; GetWindowRect(hwnd, &r);
    WritePrivateProfileString(L"Settings", L"x",       std::to_wstring(r.left).c_str(),     g_iniFile);
    WritePrivateProfileString(L"Settings", L"y",       std::to_wstring(r.top).c_str(),      g_iniFile);
    WritePrivateProfileString(L"Settings", L"mode",    std::to_wstring(currentMode).c_str(),g_iniFile);
    WritePrivateProfileString(L"Settings", L"click",   clickThrough ? L"1" : L"0",          g_iniFile);
    WritePrivateProfileString(L"Settings", L"bg",      useNoBG      ? L"1" : L"0",          g_iniFile);
    WritePrivateProfileString(L"Settings", L"ampm",    useAMPM      ? L"1" : L"0",          g_iniFile);
    WritePrivateProfileString(L"Settings", L"opacity", std::to_wstring(opacity).c_str(),    g_iniFile);
    WritePrivateProfileString(L"Settings", L"fadeIn",  std::to_wstring(fadeInMs).c_str(),   g_iniFile);
    WritePrivateProfileString(L"Settings", L"fadeOut", std::to_wstring(fadeOutMs).c_str(),  g_iniFile);
}

void LoadSettings(int& x, int& y)
{
    x = GetPrivateProfileInt(L"Settings", L"x",       100, g_iniFile);
    y = GetPrivateProfileInt(L"Settings", L"y",       100, g_iniFile);
    currentMode  = (Mode)GetPrivateProfileInt(L"Settings", L"mode",    0,   g_iniFile);
    clickThrough =       GetPrivateProfileInt(L"Settings", L"click",   0,   g_iniFile) != 0;
    useNoBG      =       GetPrivateProfileInt(L"Settings", L"bg",      0,   g_iniFile) != 0;
    useAMPM      =       GetPrivateProfileInt(L"Settings", L"ampm",    0,   g_iniFile) != 0;
    opacity      =       GetPrivateProfileInt(L"Settings", L"opacity", 255, g_iniFile);
    fadeInMs     =       GetPrivateProfileInt(L"Settings", L"fadeIn",  800, g_iniFile);
    fadeOutMs    =       GetPrivateProfileInt(L"Settings", L"fadeOut", 600, g_iniFile);
}

// ============================================================
// ВРЕМЯ
// ============================================================
void UpdateTime()
{
    time_t t = time(0);
    tm now; localtime_s(&now, &t);

    int prevSec = g_time.second;
    int prevMin = g_time.minute;

    g_time.second = now.tm_sec;
    g_time.minute = now.tm_min;
    g_time.hour   = now.tm_hour;
    g_time.isAMPM = useAMPM;

    int hour = useAMPM ? (now.tm_hour % 12 == 0 ? 12 : now.tm_hour % 12) : now.tm_hour;
    int newDigits[4] = { hour / 10, hour % 10, now.tm_min / 10, now.tm_min % 10 };

    for (int i = 0; i < 4; i++) {
        if (newDigits[i] != g_prevDigits[i]) {
            FireAddonDigitChange(i, g_prevDigits[i], newDigits[i]);
            g_prevDigits[i] = newDigits[i];
        }
    }
    if (g_time.minute != prevMin) FireAddonMinute();
}

// ============================================================
// РЕНДЕР
// ============================================================
void DrawTime(Graphics& g)
{
    int hour = useAMPM ? (g_time.hour % 12 == 0 ? 12 : g_time.hour % 12) : g_time.hour;
    int idx[4] = { hour / 10, hour % 10, g_time.minute / 10, g_time.minute % 10 };
    int x = 10;
    for (int i = 0; i < 4; i++) {
        if (digits[idx[i]]) g.DrawImage(digits[idx[i]], x, 10, 60, 80);
        x += (i == 1) ? 85 : 65;
    }
}

void Render(HWND hwnd, BYTE alpha)
{
    HDC screenDC = GetDC(NULL);
    HDC memDC    = CreateCompatibleDC(screenDC);

    const int W = 300, H = 120;
    BITMAPINFO bmi = {};
    bmi.bmiHeader.biSize        = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth       = W;
    bmi.bmiHeader.biHeight      = -H;
    bmi.bmiHeader.biPlanes      = 1;
    bmi.bmiHeader.biBitCount    = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    void* bits;
    HBITMAP bmp = CreateDIBSection(screenDC, &bmi, DIB_RGB_COLORS, &bits, NULL, 0);
    HGDIOBJ old = SelectObject(memDC, bmp);

    Graphics g(memDC);
    g.Clear(Color(0,0,0,0));
    DrawTime(g);
    FireAddonRender(&g, W, H);   // аддоны рисуют поверх

    RECT r; GetWindowRect(hwnd, &r);
    POINT ptPos = { r.left, r.top }, ptSrc = { 0,0 };
    SIZE  size  = { W, H };
    BLENDFUNCTION blend = { AC_SRC_OVER, 0, alpha, AC_SRC_ALPHA };
    UpdateLayeredWindow(hwnd, screenDC, &ptPos, &size, memDC, &ptSrc, 0, &blend, ULW_ALPHA);

    SelectObject(memDC, old);
    DeleteObject(bmp);
    DeleteDC(memDC);
    ReleaseDC(NULL, screenDC);
}

// ============================================================
// FADE
// ============================================================
void StartFadeIn(HWND hwnd)  { fadeState=FADE_IN;  fadeCurrent=0;       exitAfterFade=false; SetTimer(hwnd,IDT_FADE,fadeStep,NULL); }
void StartFadeOut(HWND hwnd, bool ex) { fadeState=FADE_OUT; fadeCurrent=opacity; exitAfterFade=ex; SetTimer(hwnd,IDT_FADE,fadeStep,NULL); }

// ============================================================
// ТРЕЙ
// ============================================================
void AddTrayIcon(HWND hwnd)
{
    g_nid.cbSize           = sizeof(g_nid);
    g_nid.hWnd             = hwnd;
    g_nid.uID              = TRAY_ID;
    g_nid.uFlags           = NIF_ICON | NIF_TIP | NIF_MESSAGE;
    g_nid.uCallbackMessage = WM_TRAYICON;
    g_nid.hIcon            = LoadIcon(NULL, IDI_APPLICATION);
    wcscpy_s(g_nid.szTip, L"Anime Clock Widget");
    Shell_NotifyIcon(NIM_ADD, &g_nid);
}

void RemoveTrayIcon()
{
    Shell_NotifyIcon(NIM_DELETE, &g_nid);
}

// ============================================================
// DESKTOP MODE
// ============================================================
void AttachToDesktop(HWND hwnd)
{
    HWND progman = FindWindow(L"Progman", NULL);
    SendMessage(progman, 0x052C, 0, 0);
    HWND workerw = NULL;
    EnumWindows([](HWND top, LPARAM param) -> BOOL {
        HWND shell = FindWindowEx(top, NULL, L"SHELLDLL_DefView", NULL);
        if (shell) { HWND* r=(HWND*)param; *r=FindWindowEx(NULL,top,L"WorkerW",NULL); }
        return TRUE;
    }, (LPARAM)&workerw);
    if (workerw) SetParent(hwnd, workerw);
}

void ApplyMode(HWND hwnd)
{
    if      (currentMode==DESKTOP) AttachToDesktop(hwnd);
    else if (currentMode==NORMAL)  { SetParent(hwnd,NULL); SetWindowPos(hwnd,HWND_NOTOPMOST,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE); }
    else                           { SetParent(hwnd,NULL); SetWindowPos(hwnd,HWND_TOPMOST,  0,0,0,0,SWP_NOMOVE|SWP_NOSIZE); }
}

void UpdateClickMode(HWND hwnd)
{
    LONG ex = GetWindowLong(hwnd, GWL_EXSTYLE);
    if (clickThrough) SetWindowLong(hwnd,GWL_EXSTYLE,ex| WS_EX_TRANSPARENT);
    else              SetWindowLong(hwnd,GWL_EXSTYLE,ex&~WS_EX_TRANSPARENT);
}

// ============================================================
// SETTINGS DIALOG
// ============================================================
#define IDC_OPACITY  201
#define IDC_FADEIN   202
#define IDC_FADEOUT  203
#define IDC_VAL_OP   207
#define IDC_VAL_FI   208
#define IDC_VAL_FO   209

LRESULT CALLBACK SettingsWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg) {
    case WM_HSCROLL: {
        HWND tr = (HWND)lParam;
        int v   = (int)SendMessage(tr, TBM_GETPOS, 0, 0);
        int id  = GetDlgCtrlID(tr);
        if (id==IDC_OPACITY) { opacity  =v; SetDlgItemText(hwnd,IDC_VAL_OP,std::to_wstring(v).c_str()); }
        if (id==IDC_FADEIN)  { fadeInMs =v; SetDlgItemText(hwnd,IDC_VAL_FI,std::to_wstring(v).c_str()); }
        if (id==IDC_FADEOUT) { fadeOutMs=v; SetDlgItemText(hwnd,IDC_VAL_FO,std::to_wstring(v).c_str()); }
        return 0;
    }
    case WM_COMMAND:
        if (LOWORD(wParam)==IDOK) DestroyWindow(hwnd);
        return 0;
    case WM_KEYDOWN:
        if (wParam==VK_ESCAPE) DestroyWindow(hwnd);
        return 0;
    case WM_DESTROY:
        EnableWindow(GetWindow(hwnd,GW_OWNER),TRUE);
        return 0;
    }
    return DefWindowProc(hwnd,msg,wParam,lParam);
}

void ShowSettingsDialog(HWND parent)
{
    static bool reg=false;
    if (!reg) {
        WNDCLASS wc={};
        wc.lpfnWndProc=SettingsWndProc; wc.hInstance=g_hInst;
        wc.hbrBackground=(HBRUSH)(COLOR_BTNFACE+1);
        wc.lpszClassName=L"ClockSettings";
        RegisterClass(&wc); reg=true;
    }
    EnableWindow(parent,FALSE);
    RECT pr; GetWindowRect(parent,&pr);
    HWND dlg=CreateWindowEx(WS_EX_DLGMODALFRAME|WS_EX_TOPMOST,
        L"ClockSettings",L"Settings",
        WS_POPUP|WS_CAPTION|WS_SYSMENU|WS_VISIBLE,
        (pr.left+pr.right)/2-165,(pr.top+pr.bottom)/2-115,330,235,
        parent,NULL,g_hInst,NULL);
    if (!dlg) { EnableWindow(parent,TRUE); return; }

    auto lbl=[&](const wchar_t* t,int y){ CreateWindow(L"STATIC",t,WS_CHILD|WS_VISIBLE,10,y,90,20,dlg,NULL,g_hInst,NULL); };
    auto mkT=[&](int id,int y,int lo,int hi,int cur){
        HWND t=CreateWindow(TRACKBAR_CLASS,NULL,WS_CHILD|WS_VISIBLE|TBS_HORZ|TBS_NOTICKS,100,y,165,28,dlg,(HMENU)(UINT_PTR)id,g_hInst,NULL);
        SendMessage(t,TBM_SETRANGE,TRUE,MAKELPARAM(lo,hi)); SendMessage(t,TBM_SETPOS,TRUE,cur);
    };
    auto mkV=[&](int id,int v,int y){ CreateWindow(L"STATIC",std::to_wstring(v).c_str(),WS_CHILD|WS_VISIBLE,272,y,50,20,dlg,(HMENU)(UINT_PTR)id,g_hInst,NULL); };

    lbl(L"Opacity:",      18); mkT(IDC_OPACITY,13, 10, 255,opacity);  mkV(IDC_VAL_OP,opacity, 18);
    lbl(L"Fade in (ms):", 68); mkT(IDC_FADEIN, 63,100,5000,fadeInMs); mkV(IDC_VAL_FI,fadeInMs,68);
    lbl(L"Fade out (ms):",118);mkT(IDC_FADEOUT,113,100,5000,fadeOutMs);mkV(IDC_VAL_FO,fadeOutMs,118);
    CreateWindow(L"BUTTON",L"OK",WS_CHILD|WS_VISIBLE|BS_DEFPUSHBUTTON,120,170,80,28,dlg,(HMENU)IDOK,g_hInst,NULL);

    MSG m;
    while (IsWindow(dlg) && GetMessage(&m,NULL,0,0)) { TranslateMessage(&m); DispatchMessage(&m); }
    EnableWindow(parent,TRUE); SetForegroundWindow(parent);
}

// ============================================================
// ADDONS DIALOG
// ============================================================
#define IDC_ADDON_LIST  300
#define IDC_ADDON_OK    301

LRESULT CALLBACK AddonsDlgProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg) {
    case WM_INITDIALOG: {
        HWND lb = GetDlgItem(hwnd, IDC_ADDON_LIST);
        for (int i=0; i<(int)g_addons.size(); i++) {
            wchar_t buf[128];
            swprintf_s(buf, L"%S  v%S", g_addons[i]->name, g_addons[i]->version);
            int idx = (int)SendMessage(lb, LB_ADDSTRING, 0, (LPARAM)buf);
            SendMessage(lb, LB_SETITEMDATA, idx, (LPARAM)i);
            // Чекбокс через owner-draw невозможен в обычном LB,
            // используем строки с префиксом
            // Вместо этого используем checklistbox через LBS_MULTIPLESEL + отдельный массив
        }
        return TRUE;
    }
    case WM_COMMAND:
        if (LOWORD(wParam)==IDC_ADDON_OK || LOWORD(wParam)==IDOK)
            EndDialog(hwnd, IDOK);
        return TRUE;
    case WM_CLOSE:
        EndDialog(hwnd, IDCANCEL);
        return TRUE;
    }
    return FALSE;
}

void ShowAddonsDialog(HWND parent)
{
    static bool reg=false;
    if (!reg) {
        WNDCLASS wc={};
        wc.lpfnWndProc=DefWindowProc; wc.hInstance=g_hInst;
        wc.hbrBackground=(HBRUSH)(COLOR_BTNFACE+1);
        wc.lpszClassName=L"ClockAddons";
        RegisterClass(&wc); reg=true;
    }

    EnableWindow(parent, FALSE);

    RECT pr; GetWindowRect(parent, &pr);
    int cx=(pr.left+pr.right)/2-175;
    int cy=(pr.top+pr.bottom)/2-150;

    HWND dlg=CreateWindowEx(WS_EX_DLGMODALFRAME|WS_EX_TOPMOST,
        L"ClockAddons", L"Addons",
        WS_POPUP|WS_CAPTION|WS_SYSMENU|WS_VISIBLE,
        cx,cy,350,320,parent,NULL,g_hInst,NULL);
    if (!dlg) { EnableWindow(parent,TRUE); return; }

    // Заголовок
    CreateWindow(L"STATIC",L"Installed addons (check = enabled):",
        WS_CHILD|WS_VISIBLE,10,10,320,20,dlg,NULL,g_hInst,NULL);

    // Список с чекбоксами
    HWND lb=CreateWindow(L"LISTBOX",NULL,
        WS_CHILD|WS_VISIBLE|WS_BORDER|WS_VSCROLL|LBS_NOTIFY,
        10,35,320,200,dlg,(HMENU)IDC_ADDON_LIST,g_hInst,NULL);

    if (g_addons.empty()) {
        SendMessage(lb,LB_ADDSTRING,0,(LPARAM)L"No addons found in addons\\");
    } else {
        for (int i=0;i<(int)g_addons.size();i++) {
            wchar_t buf[128];
            swprintf_s(buf,L"[%s] %S  v%S",
                g_addons[i]->enabled ? L"X" : L" ",
                g_addons[i]->name, g_addons[i]->version);
            int idx=(int)SendMessage(lb,LB_ADDSTRING,0,(LPARAM)buf);
            SendMessage(lb,LB_SETITEMDATA,idx,(LPARAM)i);
        }
    }

    CreateWindow(L"STATIC",L"Click to toggle on/off. Changes apply immediately.",
        WS_CHILD|WS_VISIBLE,10,240,320,20,dlg,NULL,g_hInst,NULL);

    HWND btnOK=CreateWindow(L"BUTTON",L"OK",
        WS_CHILD|WS_VISIBLE|BS_DEFPUSHBUTTON,
        130,265,80,28,dlg,(HMENU)IDC_ADDON_OK,g_hInst,NULL);

    ShowWindow(dlg,SW_SHOW); UpdateWindow(dlg);

    MSG m;
    while (IsWindow(dlg) && GetMessage(&m,NULL,0,0))
    {
        // Клик по элементу списка — toggle
        if (m.message==WM_COMMAND && LOWORD(m.wParam)==IDC_ADDON_LIST
            && HIWORD(m.wParam)==LBN_SELCHANGE)
        {
            int sel=(int)SendMessage(lb,LB_GETCURSEL,0,0);
            if (sel!=LB_ERR && !g_addons.empty()) {
                int idx=(int)SendMessage(lb,LB_GETITEMDATA,sel,0);
                if (idx>=0 && idx<(int)g_addons.size()) {
                    AddonEntry* a=g_addons[idx];
                    a->enabled=!a->enabled;
                    if (a->enabled && !a->hModule)  ReloadAddon(a);
                    if (!a->enabled && a->hModule)  UnloadAddon(a);

                    wchar_t buf[128];
                    swprintf_s(buf,L"[%s] %S  v%S",
                        a->enabled?L"X":L" ", a->name, a->version);
                    SendMessage(lb,LB_DELETESTRING,sel,0);
                    SendMessage(lb,LB_INSERTSTRING,sel,(LPARAM)buf);
                    SendMessage(lb,LB_SETITEMDATA,sel,(LPARAM)idx);
                }
            }
        }
        if (m.message==WM_COMMAND &&
            (LOWORD(m.wParam)==IDC_ADDON_OK||LOWORD(m.wParam)==IDOK))
        { DestroyWindow(dlg); break; }
        if (m.message==WM_KEYDOWN && m.wParam==VK_ESCAPE)
        { DestroyWindow(dlg); break; }

        TranslateMessage(&m); DispatchMessage(&m);
    }

    EnableWindow(parent,TRUE); SetForegroundWindow(parent);
}

// ============================================================
// CONTEXT MENU
// ============================================================
void ShowContextMenu(HWND hwnd)
{
    HMENU menu=CreatePopupMenu();
    AppendMenu(menu,MF_STRING,1,L"Desktop mode");
    AppendMenu(menu,MF_STRING,2,L"Window mode");
    AppendMenu(menu,MF_STRING,3,L"Topmost");
    AppendMenu(menu,MF_SEPARATOR,0,NULL);
    AppendMenu(menu,MF_STRING,5,clickThrough?L"Unpin":L"Pin");
    AppendMenu(menu,MF_STRING,6,useNoBG?L"Show background":L"Hide background");
    AppendMenu(menu,MF_STRING,7,useAMPM?L"24h format":L"AM/PM format");
    AppendMenu(menu,MF_SEPARATOR,0,NULL);

    // Подменю аддонов с их пунктами (если есть)
    HMENU addonSub=CreatePopupMenu();
    AppendMenu(addonSub,MF_STRING,900,L"Manage addons...");
    int addonCmdBase=1000;
    for (auto* a : g_addons) {
        if (!a->enabled || a->menuItems.empty()) continue;
        AppendMenu(addonSub,MF_SEPARATOR,0,NULL);
        wchar_t buf[64]; swprintf_s(buf,L"— %S —",a->name);
        AppendMenu(addonSub,MF_STRING|MF_DISABLED|MF_GRAYED,0,buf);
        for (auto& mi : a->menuItems) {
            AppendMenu(addonSub,MF_STRING,addonCmdBase++,mi.label.c_str());
        }
    }
    AppendMenu(menu,MF_POPUP,(UINT_PTR)addonSub,L"Addons");

    AppendMenu(menu,MF_SEPARATOR,0,NULL);
    AppendMenu(menu,MF_STRING,8,L"Settings...");
    AppendMenu(menu,MF_SEPARATOR,0,NULL);
    AppendMenu(menu,MF_STRING,4,L"Exit");

    POINT pt; GetCursorPos(&pt);
    SetForegroundWindow(hwnd); // нужно для правильного закрытия меню
    int cmd=TrackPopupMenu(menu,TPM_RETURNCMD|TPM_RIGHTBUTTON,pt.x,pt.y,0,hwnd,NULL);
    DestroyMenu(menu);

    if (cmd==1) currentMode=DESKTOP;
    if (cmd==2) currentMode=NORMAL;
    if (cmd==3) currentMode=TOPMOST;
    if (cmd==4) { SaveSettings(hwnd); StartFadeOut(hwnd,true); return; }
    if (cmd==5) clickThrough=!clickThrough;
    if (cmd==6) { useNoBG=!useNoBG; LoadImages(); }
    if (cmd==7) useAMPM=!useAMPM;
    if (cmd==8) { ShowSettingsDialog(hwnd); SaveSettings(hwnd); Render(hwnd,(BYTE)opacity); return; }
    if (cmd==900) { ShowAddonsDialog(hwnd); return; }

    // Пункты аддонов
    if (cmd>=1000) {
        int base=1000;
        for (auto* a : g_addons) {
            if (!a->enabled) continue;
            for (auto& mi : a->menuItems) {
                if (base==cmd) { mi.cb(); return; }
                base++;
            }
        }
    }

    ApplyMode(hwnd);
    UpdateClickMode(hwnd);
    SaveSettings(hwnd);
}

// ============================================================
// WNDPROC
// ============================================================
LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    static DWORD lastTick = 0;

    switch (msg) {
    case WM_CREATE:
        SetTimer(hwnd,IDT_CLOCK,1000,NULL);
        SetTimer(hwnd,IDT_ANIM, 16,  NULL); // ~60fps для аддонов
        AddTrayIcon(hwnd);
        StartFadeIn(hwnd);
        break;

    case WM_TIMER:
        if (wParam==IDT_CLOCK) {
            UpdateTime();
            if (fadeState==FADE_NONE) Render(hwnd,(BYTE)opacity);
        }
        else if (wParam==IDT_ANIM) {
            DWORD now=GetTickCount();
            float dt=(lastTick==0)?0.016f:(now-lastTick)/1000.0f;
            lastTick=now;
            FireAddonTick(dt);
        }
        else if (wParam==IDT_FADE) {
            if (fadeState==FADE_IN) {
                int d=max(1,opacity*(int)fadeStep/fadeInMs);
                fadeCurrent=min(fadeCurrent+d,opacity);
                Render(hwnd,(BYTE)fadeCurrent);
                if (fadeCurrent>=opacity){fadeState=FADE_NONE;KillTimer(hwnd,IDT_FADE);}
            } else if (fadeState==FADE_OUT) {
                int d=max(1,opacity*(int)fadeStep/fadeOutMs);
                fadeCurrent=max(fadeCurrent-d,0);
                Render(hwnd,(BYTE)fadeCurrent);
                if (fadeCurrent<=0){fadeState=FADE_NONE;KillTimer(hwnd,IDT_FADE);if(exitAfterFade)DestroyWindow(hwnd);}
            }
        }
        break;

    case WM_LBUTTONDOWN:
        if (fadeState!=FADE_NONE) break;
        dragging=true; GetCursorPos(&dragStart);
        { RECT r; GetWindowRect(hwnd,&r); windowStart={r.left,r.top}; }
        SetCapture(hwnd);
        break;

    case WM_MOUSEMOVE:
        if (dragging) {
            POINT cur; GetCursorPos(&cur);
            SetWindowPos(hwnd,NULL,
                windowStart.x+(cur.x-dragStart.x),
                windowStart.y+(cur.y-dragStart.y),
                0,0,SWP_NOSIZE|SWP_NOZORDER);
            Render(hwnd,(BYTE)opacity);
        }
        break;

    case WM_LBUTTONUP:
        dragging=false; ReleaseCapture(); SaveSettings(hwnd);
        break;

    case WM_RBUTTONUP:
        ShowContextMenu(hwnd);
        break;

    // Клик по иконке в трее
    case WM_TRAYICON:
        if (lParam==WM_RBUTTONUP || lParam==WM_LBUTTONUP)
            ShowContextMenu(hwnd);
        break;

    case WM_DESTROY:
        KillTimer(hwnd,IDT_CLOCK);
        KillTimer(hwnd,IDT_FADE);
        KillTimer(hwnd,IDT_ANIM);
        RemoveTrayIcon();
        for (auto* a:g_addons) { UnloadAddon(a); delete a; }
        g_addons.clear();
        PostQuitMessage(0);
        break;
    }
    return DefWindowProc(hwnd,msg,wParam,lParam);
}

// ============================================================
// WINMAIN
// ============================================================
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int)
{
    g_hInst=hInstance;

    HANDLE mutex=CreateMutex(NULL,TRUE,L"AnimeClockWidgetMutex");
    if (GetLastError()==ERROR_ALREADY_EXISTS) return 0;

    InitCommonControls();

    GdiplusStartupInput gsi;
    GdiplusStartup(&gdiplusToken,&gsi,NULL);

    InitPaths();
    InitAPI();

    int x,y;
    LoadSettings(x,y);
    LoadImages();
    UpdateTime();

    WNDCLASS wc={};
    wc.lpfnWndProc=WndProc;
    wc.hInstance=hInstance;
    wc.lpszClassName=L"ClockWidget";
    RegisterClass(&wc);

    HWND hwnd=CreateWindowEx(
        WS_EX_LAYERED,
        wc.lpszClassName,L"Anime Clock",
        WS_POPUP,
        x,y,300,120,
        NULL,NULL,hInstance,NULL);

    g_hwnd=hwnd;
    g_api.hwnd=hwnd;

    ShowWindow(hwnd,SW_SHOW);
    ApplyMode(hwnd);
    UpdateClickMode(hwnd);

    // Загрузить аддоны после создания окна
    ScanAndLoadAddons();

    MSG msg={};
    while (GetMessage(&msg,NULL,0,0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    GdiplusShutdown(gdiplusToken);
    CloseHandle(mutex);
    return 0;
}
