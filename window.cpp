// Window lifetime outside the main view: tray icon, minimize/restore, window size and the global hotkey.
#include "app.hpp"
#include "log.hpp"
#include <shellapi.h>

bool App::UpdateHotkey() {
    if (hotkeyRegistered) {
        UnregisterHotKey(window, ShowWindowHotkey);
        hotkeyRegistered = false;
    }
    const int option = library.prefs.globalHotkey;
    if (option <= 0 || option >= HotkeyOptionCount) return true;
    const auto& hotkey = ShowWindowHotkeys[option];
    hotkeyRegistered = RegisterHotKey(window, ShowWindowHotkey, hotkey.modifiers | MOD_NOREPEAT, hotkey.key) != FALSE;
    if (!hotkeyRegistered) {
        Log(L"RegisterHotKey(%s) failed with error %lu", HotkeyLabels[option].Get(), GetLastError());
    }
    return hotkeyRegistered;
}

void App::ToggleFromHotkey() {
    if (!inTray && IsWindowVisible(window) && !IsIconic(window) && GetForegroundWindow() == window) {
        ShowWindow(window, SW_MINIMIZE);
        return;
    }
    Restore();
}

bool App::AddTray() {
    NOTIFYICONDATAW icon{sizeof(icon)};
    icon.hWnd = window;
    icon.uID = 1;
    icon.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP | NIF_SHOWTIP;
    icon.uCallbackMessage = TrayCallback;
    icon.hIcon = reinterpret_cast<HICON>(GetClassLongPtrW(window, GCLP_HICONSM));
    wcscpy_s(icon.szTip, Texts::AppName.Get());
    if (!Shell_NotifyIconW(NIM_ADD, &icon)) return false;
    icon.uVersion = NOTIFYICON_VERSION_4;
    Shell_NotifyIconW(NIM_SETVERSION, &icon);
    return true;
}

void App::Suspend(bool suspended) {
    minimized = suspended;
    PauseIconWorker(suspended);
    SyncAmbient();
    if (!suspended) {
        ScheduleIcons();
        return;
    }
    CloseMenu();
    KillTimer(window, AnimationTimer);
    transitions.clear();
    hoverMotion.clear();
    pressMotion.clear();
    dialogSince = 0;
    tileImageFadeUntil = 0;
    hover = 0;
    hotId.clear();
}

bool App::SendToTray() {
    if (inTray) return true;
    if (!AddTray()) return false;
    CaptureWindowSize();
    SaveWindowSize();
    inTray = true;
    Suspend(true);
    ShowWindow(window, SW_HIDE);
    return true;
}

void App::RemoveTray() {
    if (!inTray) return;
    NOTIFYICONDATAW icon{sizeof(icon)};
    icon.hWnd = window;
    icon.uID = 1;
    Shell_NotifyIconW(NIM_DELETE, &icon);
    inTray = false;
}

void App::CaptureWindowSize() {
    if (!windowShown || !IsWindowVisible(window) || IsIconic(window)) return;
    windowSize.maximized = IsZoomed(window) != FALSE;
    if (windowSize.maximized) return;
    RECT rect{};
    if (!GetWindowRect(window, &rect)) return;
    const UINT windowDpi = GetDpiForWindow(window);
    if (!windowDpi) return;
    const int normalWidth = MulDiv(rect.right - rect.left, 96, windowDpi);
    const int normalHeight = MulDiv(rect.bottom - rect.top, 96, windowDpi);
    if (normalWidth > 0 && normalHeight > 0) {
        windowSize.width = normalWidth;
        windowSize.height = normalHeight;
    }
}

void App::SaveWindowSize() {
    if (windowShown) library.SetWindowSize(windowSize);
}

void App::ShowInitialWindow(int command) {
    const bool maximize = windowSize.maximized;
    const bool showNormally = command == SW_SHOWNORMAL || command == SW_SHOW || command == SW_SHOWDEFAULT;
    windowShown = command != SW_HIDE;
    ShowWindow(window, maximize && showNormally ? SW_SHOWMAXIMIZED : command);
    if (maximize && showNormally && IsWindowVisible(window) && !IsZoomed(window)) {
        ShowWindow(window, SW_SHOWMAXIMIZED);
    }
    CaptureWindowSize();
    SyncAmbient();
}

void App::Restore() {
    KillTimer(window, StartupTrayTimer);
    RefreshPowerStatus();
    UpdateMotion();
    RemoveTray();
    if (!windowShown) ShowInitialWindow(SW_SHOWNORMAL);
    else if (IsIconic(window)) ShowWindow(window, SW_RESTORE);
    else if (!IsWindowVisible(window)) ShowWindow(window, SW_SHOW);
    Suspend(false);
    if (search) Layout();
    SetForegroundWindow(window);
}

void App::TrayMenu(POINT position) {
    HMENU menu = CreatePopupMenu();
    if (!menu) return;
    if (position.x == -1 && position.y == -1) GetCursorPos(&position);
    AppendMenuW(menu, MF_STRING, 1, Texts::OpenApp.Get());
    AppendMenuW(menu, MF_STRING, 2, Texts::Exit.Get());
    SetMenuDefaultItem(menu, 1, FALSE);
    SetForegroundWindow(window);
    const UINT action = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, position.x, position.y, 0, window, nullptr);
    DestroyMenu(menu);
    if (action == 1) Restore();
    else if (action == 2) DestroyWindow(window);
    else PostMessageW(window, WM_NULL, 0, 0);
}
