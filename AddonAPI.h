#pragma once
#include <windows.h>
#include <gdiplus.h>

// -------------------------------------------------------
// Anime Clock Widget — Addon API v1
// -------------------------------------------------------
// Каждый .dll аддон должен экспортировать:
//   extern "C" __declspec(dllexport) const char* AddonName();
//   extern "C" __declspec(dllexport) const char* AddonVersion();
//   extern "C" __declspec(dllexport) void AddonInit(ClockAPI* api);
//   extern "C" __declspec(dllexport) void AddonShutdown();
// -------------------------------------------------------

struct ClockTime {
    int hour;    // 0-23 всегда (24h внутри)
    int minute;
    int second;
    bool isAMPM; // настройка пользователя
};

// Колбэк-типы
typedef void (*CB_Tick)       (float dt);                          // dt в секундах
typedef void (*CB_Render)     (Gdiplus::Graphics* g, int w, int h);// рисовать поверх виджета
typedef void (*CB_DigitChange)(int digitIdx, int oldVal, int newVal);
typedef void (*CB_Minute)     ();
typedef void (*CB_MenuItem)   ();

struct ClockAPI
{
    // --- Версия API ---
    int apiVersion; // = 1

    // --- Текущее время (обновляется каждый тик) ---
    const ClockTime* time;

    // --- HWND главного окна ---
    HWND hwnd;

    // --- Регистрация колбэков ---
    // Несколько аддонов могут регистрировать один и тот же тип колбэка
    void (*OnTick)        (CB_Tick cb);
    void (*OnRender)      (CB_Render cb);
    void (*OnDigitChange) (CB_DigitChange cb);
    void (*OnMinute)      (CB_Minute cb);

    // --- Контекстное меню ---
    // Добавляет пункт в подменю аддона (отображается в Addons → <AddonName>)
    void (*AddMenuItem)(const wchar_t* label, CB_MenuItem cb);

    // --- Утилиты ---
    // Загрузить изображение (путь относительно папки аддона)
    Gdiplus::Image* (*LoadImage)(const wchar_t* addonName, const wchar_t* relativePath);

    // Логирование → addon_log.txt рядом с exe
    void (*Log)(const wchar_t* addonName, const wchar_t* message);

    // Настройки аддона (сохраняются в addons\<addonName>\settings.ini)
    void           (*SaveSetting)(const wchar_t* addonName, const wchar_t* key, const wchar_t* value);
    const wchar_t* (*LoadSetting)(const wchar_t* addonName, const wchar_t* key, const wchar_t* defaultVal);

    // Получить абсолютный путь к папке аддона (addons\<addonName>\)
    const wchar_t* (*GetAddonDir)(const wchar_t* addonName);

    // Принудительно перерисовать виджет
    void (*Redraw)();
};

// -------------------------------------------------------
// Типы экспортируемых функций (для LoadLibrary / GetProcAddress)
// -------------------------------------------------------
typedef const char* (*FN_AddonName)   ();
typedef const char* (*FN_AddonVersion)();
typedef void        (*FN_AddonInit)   (ClockAPI*);
typedef void        (*FN_AddonShutdown)();
