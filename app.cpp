#include "app.hpp"
#include "log.hpp"
#include <dwmapi.h>
#include <commctrl.h>
#include <shellapi.h>
#include <shlobj.h>
#include <uxtheme.h>
#include <powrprof.h>
#include <algorithm>
#include <cmath>
#include <thread>

namespace {
constexpr int MinimumWindowWidth = 485, MinimumWindowHeight = 475;
constexpr wchar_t WindowClass[] = L"Polytsia.Launcher";

constexpr int EditSearchId = 201;
constexpr int EditNameId = 202;

std::wstring WindowText(HWND window) {
    const int length = GetWindowTextLengthW(window);
    std::wstring result(static_cast<size_t>(length) + 1, L'\0');
    result.resize(GetWindowTextW(window, result.data(), length + 1));
    return result;
}

bool SystemDark() {
    DWORD light = 1, size = sizeof(light);
    RegGetValueW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
                 L"AppsUseLightTheme", RRF_RT_REG_DWORD, nullptr, &light, &size);
    return light == 0;
}

bool SystemTransparency() {
    DWORD enabled = 1, size = sizeof(enabled);
    RegGetValueW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
                 L"EnableTransparency", RRF_RT_REG_DWORD, nullptr, &enabled, &size);
    return enabled != 0;
}

bool TrayIconRegistered(HWND window) {
    NOTIFYICONIDENTIFIER identifier{sizeof(identifier)};
    identifier.hWnd = window;
    identifier.uID = 1;
    RECT bounds{};
    return SUCCEEDED(Shell_NotifyIconGetRect(&identifier, &bounds));
}

LRESULT CALLBACK EditKeys(HWND hwnd, UINT message, WPARAM w, LPARAM l, UINT_PTR, DWORD_PTR data) {
    auto& app = *reinterpret_cast<App*>(data);
    if (message == WM_KEYDOWN && app.NavigationKey(static_cast<UINT>(w))) return 0;
    if (message == WM_CHAR && (w == VK_TAB || w == VK_ESCAPE || w == VK_RETURN || w == 6)) return 0;
    if (message == WM_PAINT) {
        PAINTSTRUCT paint{};
        HDC dc = BeginPaint(hwnd, &paint);
        RECT area{};
        GetClientRect(hwnd, &area);
        HDC buffer = nullptr;
        HPAINTBUFFER painting = BeginBufferedPaint(dc, &area, BPBF_TOPDOWNDIB, nullptr, &buffer);
        if (painting) {
            FillRect(buffer, &area, app.fieldBrush);
            DefSubclassProc(hwnd, WM_PRINTCLIENT, reinterpret_cast<WPARAM>(buffer), PRF_CLIENT);
            if (hwnd == app.search && GetFocus() != hwnd && GetWindowTextLengthW(hwnd) == 0) {
                const auto foreground = app.theme.muted;
                const auto previous = SelectObject(buffer, app.font);
                SetBkMode(buffer, TRANSPARENT);
                SetTextColor(buffer, RGB(foreground.r * 255, foreground.g * 255, foreground.b * 255));
                DrawTextW(buffer, Texts::FindShortcut.Get(), -1, &area,
                          DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX);
                if (previous) SelectObject(buffer, previous);
            }
            BufferedPaintSetAlpha(painting, nullptr, 255);
            EndBufferedPaint(painting, TRUE);
        }
        EndPaint(hwnd, &paint);
        if (!painting) {
            InvalidateRect(hwnd, nullptr, FALSE);
            return DefSubclassProc(hwnd, message, w, l);
        }
        return 0;
    }
    return DefSubclassProc(hwnd, message, w, l);
}

LRESULT CALLBACK WindowProc(HWND hwnd, UINT message, WPARAM w, LPARAM l) {
    if (message == WM_NCCREATE) {
        const auto* creation = reinterpret_cast<CREATESTRUCTW*>(l);
        auto* app = static_cast<App*>(creation->lpCreateParams);
        app->window = hwnd;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(app));
    }
    if (auto* app = reinterpret_cast<App*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA))) return app->Handle(message, w, l);
    return DefWindowProcW(hwnd, message, w, l);
}

fs::path DataPath() {
    const DWORD length = GetEnvironmentVariableW(L"POLYTSIA_DATA_DIR", nullptr, 0);
    if (length) {
        std::wstring path(length, L'\0');
        const DWORD read = GetEnvironmentVariableW(L"POLYTSIA_DATA_DIR", path.data(), length);
        if (read && read < length) {
            path.resize(read);
            return fs::path(path);
        }
    }
    PWSTR folder = nullptr;
    fs::path path;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &folder))) {
        path = fs::path(folder) / L"Polytsia";
        CoTaskMemFree(folder);
    }
    return path;
}

bool Inside(D2D1_RECT_F box, float x, float y) {
    return x >= box.left && x < box.right && y >= box.top && y < box.bottom;
}

// 64-bit FNV-1a over the UTF-16 bytes. Deterministic across compilers and identical to what
// MSVC's std::hash<std::wstring> produced in earlier releases, so the instance mutex name is unchanged.
unsigned long long PathHash(const std::wstring& text) {
    unsigned long long hash = 14695981039346656037ull;
    const auto* bytes = reinterpret_cast<const unsigned char*>(text.data());
    for (size_t i = 0; i < text.size() * sizeof(wchar_t); ++i) {
        hash ^= bytes[i];
        hash *= 1099511628211ull;
    }
    return hash;
}

int ResizeInset(int axisMetric, UINT dpi) {
    return GetSystemMetricsForDpi(axisMetric, dpi) + GetSystemMetricsForDpi(SM_CXPADDEDBORDER, dpi);
}
}

Palette MakePalette(const ColorScheme& colors, bool dark, bool contrast) {
    auto color = [](unsigned hex, float a = 1.f) { return D2D1::ColorF(hex, a); };
    if (contrast) {
        auto system = [](int index) {
            const COLORREF c = GetSysColor(index);
            return D2D1::ColorF(GetRValue(c) / 255.f, GetGValue(c) / 255.f, GetBValue(c) / 255.f);
        };
        return {system(COLOR_WINDOW), system(COLOR_WINDOWTEXT), system(COLOR_WINDOWTEXT),
                system(COLOR_WINDOW), system(COLOR_WINDOWTEXT), system(COLOR_WINDOW), system(COLOR_BTNFACE),
                system(COLOR_HIGHLIGHT), system(COLOR_HIGHLIGHT), system(COLOR_HIGHLIGHTTEXT), system(COLOR_WINDOW), color(0, .5f)};
    }
    if (dark) {
        return {color(colors.background), color(0xF2F5FD), color(0xB9C6D5), color(0xD3DDEB, .075f),
                color(0xB5C5D8, .20f), color(colors.field), color(0xFFFFFF, .085f), color(colors.accent),
                color(colors.primary), color(0xFFFFFF), color(colors.dialog), color(0x070C16, .67f)};
    }
    return {color(colors.background), color(0x172238), color(0x4E5E76), color(0xFFFFFF, .56f),
            color(colors.accent, .19f), color(colors.field), color(0xFFFFFF, .62f), color(colors.accent),
            color(colors.primary), color(0xFFFFFF), color(colors.dialog), color(0x13203A, .35f)};
}

float Tween::Value(ULONGLONG now) const {
    const float progress = std::clamp(static_cast<float>(now - since) / duration, 0.f, 1.f);
    const float remaining = 1.f - progress;
    return from + (to - from) * (1.f - remaining * remaining * remaining);
}

App::~App() {
    if (fieldBrush) DeleteObject(fieldBrush);
    if (font) DeleteObject(font);
}

bool App::Create(HWND hwnd) {
    dpi = static_cast<float>(GetDpiForWindow(hwnd));
    taskbarCreated = RegisterWindowMessageW(L"TaskbarCreated");
    RefreshAutoStart();
    autoStart = AutoStartRegistered();
    search = CreateWindowExW(WS_EX_LAYERED, L"EDIT", nullptr, WS_CHILD | ES_AUTOHSCROLL,
                            0, 0, 0, 0, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(EditSearchId)), instance, nullptr);
    nameEdit = CreateWindowExW(0, L"EDIT", nullptr, WS_CHILD | ES_AUTOHSCROLL,
                            0, 0, 0, 0, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(EditNameId)), instance, nullptr);
    if (!search || !nameEdit) return false;
    // Keep the native text control's GDI writes off the Acrylic parent's alpha surface.
    if (!SetLayeredWindowAttributes(search, 0, 255, LWA_ALPHA)) {
        const auto style = GetWindowLongPtrW(search, GWL_EXSTYLE);
        SetWindowLongPtrW(search, GWL_EXSTYLE, style & ~static_cast<LONG_PTR>(WS_EX_LAYERED));
    }
    SetWindowSubclass(search, EditKeys, 1, reinterpret_cast<DWORD_PTR>(this));
    SetWindowSubclass(nameEdit, EditKeys, 1, reinterpret_cast<DWORD_PTR>(this));
    SendMessageW(search, EM_SETLIMITTEXT, 512, 0);
    SendMessageW(nameEdit, EM_SETLIMITTEXT, 255, 0);
    UpdateFonts();
    RefreshPowerStatus();
    ApplyTheme();
    browseDark = dark;
    DragAcceptFiles(hwnd, TRUE);
    Filter();
    UpdateHotkey();
    return true;
}

int App::ToPixel(float dip) const { return static_cast<int>(std::lround(dip * dpi / 96.f)); }

void App::UpdateFonts() {
    HFONT next = CreateFontW(-ToPixel(14), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                            OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                            DEFAULT_PITCH, L"Segoe UI Variable Text");
    if (!next) return;
    SendMessageW(search, WM_SETFONT, reinterpret_cast<WPARAM>(next), FALSE);
    SendMessageW(nameEdit, WM_SETFONT, reinterpret_cast<WPARAM>(next), FALSE);
    if (font) DeleteObject(font);
    font = next;
}

void App::RefreshPowerStatus() {
    SYSTEM_POWER_CAPABILITIES capabilities{};
    portableBattery = GetPwrCapabilities(&capabilities) &&
        capabilities.SystemBatteriesPresent && !capabilities.BatteriesAreShortTerm;
    SYSTEM_POWER_STATUS status{};
    onBattery = portableBattery && GetSystemPowerStatus(&status) && status.ACLineStatus == 0;
}

void App::UpdateMotion() {
    BOOL animations = TRUE;
    SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION, 0, &animations, 0);
    effects = animations && !contrast && library.prefs.motion &&
        !(onBattery && library.prefs.pauseMotionOnBattery);
    if (!effects) {
        const bool moveEdit = dialogSince && dialog == Dialog::Edit;
        KillTimer(window, AnimationTimer);
        transitions.clear();
        hoverMotion.clear();
        pressMotion.clear();
        dialogSince = 0;
        tileImageFadeUntil = 0;
        if (contextMenu) contextMenu->opened = 0;
        if (moveEdit) Layout();
    }
    SyncAmbient();
}

void App::ApplyTheme() {
    HIGHCONTRASTW hc{sizeof(hc)};
    SystemParametersInfoW(SPI_GETHIGHCONTRAST, sizeof(hc), &hc, 0);
    contrast = (hc.dwFlags & HCF_HIGHCONTRASTON) != 0;
    dark = library.prefs.theme == 0 ? SystemDark() : library.prefs.theme == 2;
    scheme = dark ? &DarkSchemes[library.prefs.darkPalette] : &LightSchemes[library.prefs.lightPalette];
    theme = MakePalette(*scheme, dark, contrast);
    if (fieldBrush) DeleteObject(fieldBrush);
    const auto field = theme.field;
    fieldBrush = CreateSolidBrush(RGB(field.r * 255, field.g * 255, field.b * 255));
    const BOOL darkCaption = dark && !contrast;
    // Acrylic is composed by DWM. The redirection surface's alpha is supported on 24H2+.
    const BOOL wantsGlass = !contrast && SystemTransparency();
    constexpr auto AlphaAttribute = static_cast<DWMWINDOWATTRIBUTE>(39);
    glass = wantsGlass && SUCCEEDED(DwmSetWindowAttribute(window, AlphaAttribute, &wantsGlass, sizeof(wantsGlass)));
    if (!glass) {
        const BOOL opaque = FALSE;
        DwmSetWindowAttribute(window, AlphaAttribute, &opaque, sizeof(opaque));
    }
    const DWM_SYSTEMBACKDROP_TYPE backdrop = glass ? DWMSBT_TRANSIENTWINDOW : contrast ? DWMSBT_NONE : DWMSBT_MAINWINDOW;
    DwmSetWindowAttribute(window, DWMWA_USE_IMMERSIVE_DARK_MODE, &darkCaption, sizeof(darkCaption));
    DwmSetWindowAttribute(window, DWMWA_SYSTEMBACKDROP_TYPE, &backdrop, sizeof(backdrop));
    const MARGINS frame = glass ? MARGINS{-1, -1, -1, -1} : MARGINS{0, 0, 0, 0};
    DwmExtendFrameIntoClientArea(window, &frame);
    const auto corners = DWMWCP_ROUND;
    DwmSetWindowAttribute(window, DWMWA_WINDOW_CORNER_PREFERENCE, &corners, sizeof(corners));
    const COLORREF border = DWMWA_COLOR_NONE;
    DwmSetWindowAttribute(window, DWMWA_BORDER_COLOR, &border, sizeof(border));
    InvalidateRect(search, nullptr, TRUE);
    InvalidateRect(nameEdit, nullptr, TRUE);
    UpdateMotion();
    Invalidate();
}

void App::Filter() {
    CloseMenu();
    const auto term = Fold(Trim(query));
    visible.clear();
    for (size_t i = 0; i < library.items.size(); ++i) {
        if (term.empty() || Fold(library.items[i].name).find(term) != std::wstring::npos) visible.push_back(i);
    }
    selected = -1;
    if (focused >= Tile) focused = 0;
    hover = dragTarget = 0;
    hotId.clear();
    transitions.clear();
    hoverMotion.clear();
    pressMotion.clear();
    Layout();
}

void App::View(Page next) {
    if (page == next) return;
    page = next;
    scroll = 0;
    focused = hover = 0;
    expandedSetting = 0;
    searchExpanded = false;
    if (!query.empty()) SetWindowTextW(search, L"");
    SetFocus(window);
    Filter();
}

void App::ShowSearch(bool expand) {
    searchExpanded = expand;
    if (!expand && !query.empty()) SetWindowTextW(search, L"");
    Layout();
    focused = Search;
    SetFocus(expand ? search : window);
    if (expand) SendMessageW(search, EM_SETSEL, 0, -1);
    Hot();
}

float App::TileScale() const {
    constexpr float sizes[] = {.82f, 1.f, 1.22f};
    return sizes[static_cast<int>(library.prefs.tileSize)];
}

float App::TileGap() const {
    constexpr float gaps[] = {6.f, 12.f, 22.f};
    return gaps[static_cast<int>(library.prefs.tileSpacing)];
}

float App::TileHeight() const {
    switch (library.prefs.tileStyle) {
    case TileStyle::Cover: return tileWidth + (library.prefs.labels == TileLabels::Always ? TileCaptionHeight : 0.f);
    case TileStyle::Compact: return 62.f * TileScale();
    default: return 134.f * TileScale();
    }
}

void App::Invalidate() { InvalidateRect(window, nullptr, FALSE); }

void App::Layout() {
    RECT area{};
    GetClientRect(window, &area);
    width = ToDip(area.right);
    height = std::max(0.f, ContentY(area.bottom));
    const float room = std::max(80.f, width - 52);
    constexpr float widths[] = {110.f, 139.f, 188.f};
    const float target = widths[static_cast<int>(library.prefs.tileSize)], gap = TileGap();
    columns = std::max(2, static_cast<int>((room + gap) / (target + gap)));
    tileWidth = std::min(target + 24.f, (room - (columns - 1) * gap) / columns);
    gridLeft = (width - columns * tileWidth - (columns - 1) * gap) / 2.f;
    const float rows = std::ceil(visible.size() / static_cast<float>(columns));
    maxScroll = page == Page::Settings ? std::max(0.f, SettingsBottom() - (height - 18.f)) :
        std::max(0.f, GridTop() + rows * RowHeight() - gap - (height - 22.f));
    scroll = std::clamp(scroll, 0.f, maxScroll);
    ShowWindow(search, searchExpanded && page != Page::Settings && dialog == Dialog::None && !contextMenu ? SW_SHOWNA : SW_HIDE);
    ShowWindow(nameEdit, dialog == Dialog::Edit ? SW_SHOWNA : SW_HIDE);
    if (searchExpanded && page != Page::Settings && dialog == Dialog::None) Place(search, SearchBox());
    if (dialog == Dialog::Edit) {
        const auto panel = Popup();
        Place(nameEdit, D2D1::RectF(panel.left + 26, panel.top + 83, panel.right - 26, panel.top + 119));
    }
    Invalidate();
    ScheduleIcons();
}

void App::Place(HWND edit, D2D1_RECT_F box) {
    const int left = ToPixel(box.left + 11);
    const int top = ClientY((box.top + box.bottom) / 2.f - 10);
    SetWindowPos(edit, nullptr, left, top, std::max(1, ToPixel(box.right - 11) - left),
                 ToPixel(21), SWP_NOACTIVATE | SWP_NOZORDER);
}

void App::ScheduleIcons() {
    if (iconsPosted || !window || minimized) return;
    if (page == Page::Settings ? settingsSection != SettingsSection::Themes || library.items.empty() : visible.empty()) return;
    iconsPosted = PostMessageW(window, LoadVisibleIcons, 0, 0) != FALSE;
}

UINT App::IconPixels() const {
    if (library.prefs.tileStyle != TileStyle::Cover) return ToPixel(64.f * TileScale());
    const int size = ToPixel(tileWidth);
    if (size <= 128) return 128;
    return size <= 256 ? 256 : 384;
}

UINT App::CoverPixels() const {
    return ToPixel(std::max(tileWidth, 134.f)) > 220 ? 576u : 384u;
}

std::wstring App::CoverKey(const Shortcut& entry) const {
    return entry.cover + L"@cover" + std::to_wstring(CoverPixels());
}

std::wstring App::IconKey(const Shortcut& entry, bool standard) const {
    return (standard || entry.icon.empty() ? entry.file : entry.icon) + L"@" + std::to_wstring(IconPixels());
}

IconCache* App::Icon(const Shortcut& entry, bool standard) {
    return CachedIcon(IconKey(entry, standard));
}

IconCache* App::CachedIcon(const std::wstring& key) {
    const auto it = icons.find(key);
    if (it == icons.end()) return nullptr;
    it->second.lastUsed = GetTickCount64();
    return &it->second;
}

void App::RequestIcon(const Shortcut& entry, bool standard) {
    auto key = IconKey(entry, standard);
    if (!requested.insert(key).second) return;
    QueueIcon(window, IconReady, std::move(key), library.File(entry.file),
        standard || entry.icon.empty() ? library.StandardIcon(entry) : library.File(entry.icon), IconPixels());
}

void App::RequestTileAssets(const Shortcut& entry) {
    if (library.prefs.tileStyle == TileStyle::Cover && !entry.cover.empty()) {
        auto key = CoverKey(entry);
        if (requested.insert(key).second) {
            QueueIcon(window, IconReady, std::move(key), {}, library.File(entry.cover), CoverPixels(), true);
        }
    }
    RequestIcon(entry);
}

void App::StoreIcon(IconResult result) {
    constexpr size_t CacheLimit = 24 * 1024 * 1024;
    const size_t bytes = result.pixels ? result.pixels->bytes.size() : 0;
    const auto now = GetTickCount64();
    const bool fade = bytes != 0 && effects && activeWindow && !minimized &&
        page == Page::Library && IsWindowVisible(window);
    if (!icons.emplace(std::move(result.key), IconCache{std::move(result.pixels), {}, now, {},
                                                       fade ? now : 0}).second) return;
    if (fade) {
        if (!tileImageFadeUntil) SetTimer(window, AnimationTimer, 16, nullptr);
        tileImageFadeUntil = now + TileImageFadeDuration;
    }
    iconBytes += bytes;
    while (iconBytes > CacheLimit) {
        const auto oldest = std::min_element(icons.begin(), icons.end(), [](const auto& first, const auto& second) {
            return first.second.lastUsed < second.second.lastUsed;
        });
        if (oldest->second.pixels) iconBytes -= oldest->second.pixels->bytes.size();
        requested.erase(oldest->first);
        icons.erase(oldest);
    }
}

void App::LoadIcons() {
    iconsPosted = false;
    if (minimized) return;
    if (page == Page::Settings) {
        if (settingsSection == SettingsSection::Themes) {
            const size_t count = std::min(ThemeSampleCount, library.items.size());
            for (size_t i = 0; i < count; ++i) RequestTileAssets(library.items[i]);
        }
        return;
    }
    const int first = std::max(0, static_cast<int>(scroll / RowHeight()) * columns);
    const int last = std::min(static_cast<int>(visible.size()),
        first + (std::max(1, static_cast<int>((height - GridTop()) / RowHeight())) + 2) * columns);
    for (int i = first; i < last; ++i) RequestTileAssets(library.items[visible[i]]);
}

void App::Scroll(float offset) {
    const float next = std::clamp(offset, 0.f, maxScroll);
    if (next == scroll) return;
    scroll = next;
    Invalidate();
    ScheduleIcons();
}

int App::At(float x, float y) const {
    if (contextMenu) {
        for (const auto& item : MenuItems()) {
            if (MenuEnabled(item.id) && Inside(MenuItemBox(item.id), x, y)) return item.id;
        }
        return 0;
    }
    for (auto hit = hits.rbegin(); hit != hits.rend(); ++hit) {
        if (Inside(hit->box, x, y)) return hit->id;
    }
    return 0;
}

int App::DropAt(float x, float y) const {
    if (visible.empty() || x < 22.f || x > width - 24.f || y < GridTop() - 3.f || y >= height - 18.f) return 0;
    const float gap = TileGap();
    const int row = std::max(0, static_cast<int>(std::floor((y - GridTop() + scroll + gap / 2.f) / RowHeight())));
    const int last = static_cast<int>(visible.size()) - 1;
    if (row > last / columns) return 0;
    const int column = std::clamp(static_cast<int>((x - gridLeft + gap / 2.f) / (tileWidth + gap)), 0, columns - 1);
    return Tile + std::min(row * columns + column, last);
}

void App::UpdateDrag() {
    const float x = ToDip(pointer.x), y = ContentY(pointer.y);
    dragTarget = DropAt(x, y);
    const bool overGrid = x >= 22.f && x <= width - 22.f && y >= GridTop() - 18.f && y <= height;
    int nextStep = 0;
    if (overGrid && y < GridTop() + 30.f && scroll > 0.f) nextStep = -13;
    else if (overGrid && y > height - 48.f && scroll < maxScroll) nextStep = 13;
    if (nextStep != dragStep) {
        dragStep = nextStep;
        if (dragStep) SetTimer(window, DragTimer, 28, nullptr);
        else KillTimer(window, DragTimer);
    }
    Invalidate();
}

void App::EndDrag() {
    if (dragStep) KillTimer(window, DragTimer);
    dragStep = dragTarget = 0;
    dragging = false;
}

void App::FocusTile(int position) {
    if (visible.empty()) return;
    selected = std::clamp(position, 0, static_cast<int>(visible.size()) - 1);
    focused = Tile + selected;
    const auto box = GridBox(selected);
    if (box.top < GridTop()) Scroll(scroll + box.top - GridTop());
    else if (box.bottom > height - 20) Scroll(scroll + box.bottom - height + 20);
    SetFocus(window);
    Hot();
    Invalidate();
}

void App::ChangeHover(int id) {
    if (id == hover) { Hot(); return; }
    AnimateControl(hoverMotion, hover, id);
    hover = id;
    Hot();
    Invalidate();
}

void App::ChangePress(int id) {
    if (pressed == id) return;
    AnimateControl(pressMotion, pressed, id);
    pressed = id;
}

void App::AnimateControl(std::map<int, Tween>& animations, int previous, int next) {
    if (!effects || minimized) return;
    const auto now = GetTickCount64();
    if (previous) animations[previous] = {ControlAmount(animations, previous, true), 0.f, now};
    if (next) animations[next] = {ControlAmount(animations, next, false), 1.f, now};
    SetTimer(window, AnimationTimer, 16, nullptr);
}

float App::ControlAmount(const std::map<int, Tween>& animations, int id, bool current) const {
    const auto found = animations.find(id);
    return found == animations.end() ? (current ? 1.f : 0.f) : found->second.Value(GetTickCount64());
}

void App::Hot() {
    int active = keyboard && focused >= Tile ? focused : hover;
    if (dialog != Dialog::None || page == Page::Settings || contextMenu) active = 0;
    std::wstring next;
    if (active >= Tile && static_cast<size_t>(active - Tile) < visible.size()) {
        next = library.items[visible[active - Tile]].id;
    }
    if (next == hotId) return;
    if (effects && !minimized) {
        const auto now = GetTickCount64();
        if (!hotId.empty()) {
            auto it = transitions.find(hotId);
            const float from = it == transitions.end() ? 1.f : it->second.Value(now);
            transitions[hotId] = {from, 0.f, now, TileHoverDuration};
        }
        if (!next.empty()) {
            auto it = transitions.find(next);
            const float from = it == transitions.end() ? 0.f : it->second.Value(now);
            transitions[next] = {from, 1.f, now, TileHoverDuration};
        }
        SetTimer(window, AnimationTimer, 16, nullptr);
    }
    hotId = std::move(next);
}

void App::MotionTick() {
    const auto now = GetTickCount64();
    const bool moveEdit = dialogSince && dialog == Dialog::Edit;
    const auto finished = [&](const auto& entry) { return now - entry.second.since >= entry.second.duration; };
    std::erase_if(transitions, finished);
    std::erase_if(hoverMotion, finished);
    std::erase_if(pressMotion, finished);
    if (contextMenu && contextMenu->opened && now - contextMenu->opened >= MenuDuration) contextMenu->opened = 0;
    if (dialogSince && now - dialogSince >= 160) dialogSince = 0;
    if (now >= tileImageFadeUntil) tileImageFadeUntil = 0;
    if (moveEdit) {
        const auto panel = Popup();
        Place(nameEdit, D2D1::RectF(panel.left + 26, panel.top + 83, panel.right - 26, panel.top + 119));
    }
    if (transitions.empty() && hoverMotion.empty() && pressMotion.empty() && !dialogSince && !tileImageFadeUntil &&
        (!contextMenu || !contextMenu->opened)) KillTimer(window, AnimationTimer);
    Invalidate();
}

void App::SyncAmbient() {
    const bool run = effects && AmbientValue() > 0 && activeWindow && !minimized && IsWindowVisible(window);
    if (run && !ambientSince) {
        ambientSince = GetTickCount64();
        if (!SetTimer(window, AmbientTimer, 33, nullptr)) ambientSince = 0;
    } else if (!run && ambientSince) {
        ambientElapsed += GetTickCount64() - ambientSince;
        ambientSince = 0;
        KillTimer(window, AmbientTimer);
    }
}

void App::PreviewAmbient(float x) {
    const auto track = AmbientTrack();
    const float ratio = std::clamp((x - track.left) / (track.right - track.left), 0.f, 1.f);
    ambientPreview = static_cast<int>(std::lround(ratio * AmbientMax));
    SyncAmbient();
    Invalidate();
}

void App::SaveAmbient(int value) {
    ambientPreview = -1;
    value = std::clamp(value, 0, AmbientMax);
    if (value != library.prefs.ambient) {
        auto preferences = library.prefs;
        preferences.ambient = value;
        Record(preferences);
    }
    SyncAmbient();
    Invalidate();
}

void App::Tab(bool backwards) {
    std::vector<int> order;
    if (dialog == Dialog::Edit) {
        order = {EditName, ChangeIcon, ResetIcon, EditCover};
        if (HasCover()) order.push_back(RemoveCoverEdit);
        order.insert(order.end(), {CancelEdit, SaveEdit});
    }
    else if (dialog == Dialog::Cover) {
        if (cropPreview.pixels) order = {CoverPreview, ZoomOut, ZoomIn, ResetCrop};
        order.push_back(ChooseCover);
        if (CoverRemovable()) order.push_back(RemoveCover);
        order.push_back(CancelCover);
        if (cropPreview.pixels || coverWork.remove) order.push_back(ApplyCover);
    }
    else if (dialog == Dialog::Remove) order = {CancelEdit, ConfirmRemoval};
    else if (dialog == Dialog::Restore) order = {CancelEdit, ConfirmRestore};
    else if (dialog == Dialog::Missing) order = {DismissError, ReplaceError};
    else if (dialog == Dialog::Error || dialog == Dialog::Info) order = {DismissError};
    else if (page == Page::Settings) {
        order = {Back, SettingsThemes, SettingsTiles, SettingsBehavior, SettingsBackup};
        switch (settingsSection) {
        case SettingsSection::Themes: {
            order.insert(order.end(), {ThemeSystem, ThemeLight, ThemeDark, BrowseLight, BrowseDark});
            const int first = browseDark ? FirstDarkPalette : FirstLightPalette;
            for (int i = 0; i < PaletteCount; ++i) order.push_back(first + i);
            order.push_back(AmbientControl);
            break;
        }
        case SettingsSection::Tiles:
            order.insert(order.end(), {TilesCentered, TilesCover, TilesCompact, LabelsAlways, LabelsHover, LabelsHidden,
                SizeSmall, SizeMedium, SizeLarge, SpacingTight, SpacingNormal, SpacingWide, TileHalo});
            break;
        case SettingsSection::Behavior:
            order.insert(order.end(), {AfterLaunchControl, MinimizeControl, CloseControl, AutoStartControl, HotkeyControl, Motion});
            if (portableBattery) order.push_back(MotionOnBattery);
            break;
        case SettingsSection::Backup:
            order.insert(order.end(), {BackupControl, RestoreControl, OpenDataControl});
            break;
        }
    }
    else {
        if (searchExpanded) order.push_back(Search);
        if (!visible.empty()) order.push_back(Tile + std::max(0, selected));
    }
    if (order.empty()) return;
    if (dialog == Dialog::None && page == Page::Settings) {
        if (const auto* menu = BehaviorFor(expandedSetting)) {
            auto option = std::find(order.begin(), order.end(), menu->id) + 1;
            for (int i = 0; i < static_cast<int>(menu->options.size()); ++i) {
                option = order.insert(option, FirstBehaviorOption + i) + 1;
            }
        }
    }
    if (GetFocus() == search) focused = Search;
    else if (GetFocus() == nameEdit) focused = EditName;
    const auto found = std::find(order.begin(), order.end(), focused);
    const int size = static_cast<int>(order.size());
    const int index = found == order.end() ? (backwards ? 0 : size - 1) : static_cast<int>(found - order.begin());
    const int next = order[(index + (backwards ? size - 1 : 1)) % size];
    keyboard = true;
    focused = next;
    if (page == Page::Settings && dialog == Dialog::None) RevealSetting(next);
    if (next >= Tile) FocusTile(next - Tile);
    else SetFocus(next == Search && searchExpanded ? search : next == EditName ? nameEdit : window);
    Hot();
    Invalidate();
}

int App::PaletteColumns() const {
    const float space = width - 2.f * SettingsEdge();
    return std::clamp(static_cast<int>((space + SettingsLayout::Gap) / (146.f + SettingsLayout::Gap)), 2, 5);
}

float App::SettingsEdge() const {
    return std::max(SettingsLayout::Margin, (width - SettingsLayout::MaxWidth) / 2.f);
}

float App::SettingsClipTop() const {
    if (settingsSection != SettingsSection::Themes) return SettingsLayout::BodyTop;
    const float previewHeight = std::clamp(118.f + (height - 420.f) * .23f, 118.f, 164.f);
    return SettingsLayout::BodyTop + previewHeight + SettingsLayout::PreviewMargin;
}

float App::AmbientPosition() const {
    return SettingsLayout::Palettes + ((PaletteCount - 1) / PaletteColumns() + 1) * SettingsLayout::Pitch + 18.f;
}

float App::SettingsBottom() const {
    using namespace SettingsLayout;
    switch (settingsSection) {
    case SettingsSection::Themes: return SettingsPosition(AmbientPosition() + SettingsLayout::RowHeight + 12.f);
    case SettingsSection::Tiles: return SettingsPosition(SettingsLayout::TileHalo + SettingsLayout::RowHeight + 12.f);
    case SettingsSection::Behavior: return SettingsPosition(SettingsLayout::MotionOnBattery + SettingsLayout::RowHeight + 30.f);
    case SettingsSection::Backup: return SettingsPosition(DataFolder + ChipHeight + 12.f);
    }
    return SettingsClipTop();
}

float App::SettingsPosition(float y) const {
    const auto* menu = BehaviorFor(expandedSetting);
    return SettingsClipTop() + y + (menu && y > menu->top ? menu->Expansion() : 0.f);
}

float App::PaletteOptionTop(int index) const {
    return SettingsPosition(SettingsLayout::Palettes) + index / PaletteColumns() * SettingsLayout::Pitch;
}

void App::SwitchSettings(SettingsSection section) {
    focused = SettingsThemes + static_cast<int>(section);
    if (settingsSection != section) {
        settingsSection = section;
        expandedSetting = 0;
        scroll = 0;
        ChangeHover(0);
        Layout();
    } else Invalidate();
}

void App::BrowsePalettes(bool darkPalette) {
    browseDark = darkPalette;
    focused = browseDark ? BrowseDark : BrowseLight;
    ChangeHover(0);
    Invalidate();
}

void App::RevealSetting(int id) {
    using namespace SettingsLayout;
    float top = 0, controlHeight = ChipHeight;
    if (const auto* menu = BehaviorFor(id)) {
        top = SettingsPosition(menu->top);
        controlHeight = SettingsLayout::RowHeight;
    } else if (id >= FirstLightPalette && id < Tile) {
        top = PaletteOptionTop(id - (id >= FirstDarkPalette ? FirstDarkPalette : FirstLightPalette));
    } else if (id >= FirstBehaviorOption && id < LaunchMenu) {
        const auto* expanded = BehaviorFor(expandedSetting);
        if (!expanded || id - FirstBehaviorOption >= static_cast<int>(expanded->options.size())) return;
        top = SettingsPosition(expanded->top) + SettingsLayout::RowHeight + (id - FirstBehaviorOption) * ChipHeight;
    } else {
        switch (id) {
        case ThemeSystem: case ThemeLight: case ThemeDark: top = SettingsPosition(ThemeMode); break;
        case BrowseLight: case BrowseDark: top = SettingsPosition(PaletteMode); break;
        case TilesCentered: case TilesCover: case TilesCompact: top = SettingsPosition(Tiles); break;
        case LabelsAlways: case LabelsHover: case LabelsHidden: top = SettingsPosition(Labels); break;
        case SizeSmall: case SizeMedium: case SizeLarge: top = SettingsPosition(SettingsLayout::TileSize); break;
        case SpacingTight: case SpacingNormal: case SpacingWide: top = SettingsPosition(SettingsLayout::TileSpacing); break;
        case Control::TileHalo: top = SettingsPosition(SettingsLayout::TileHalo); controlHeight = SettingsLayout::RowHeight; break;
        case Control::Motion: top = SettingsPosition(SettingsLayout::Motion); controlHeight = SettingsLayout::RowHeight; break;
        case Control::MotionOnBattery:
            top = SettingsPosition(SettingsLayout::MotionOnBattery); controlHeight = SettingsLayout::RowHeight; break;
        case AmbientControl: top = SettingsPosition(AmbientPosition()); controlHeight = SettingsLayout::RowHeight; break;
        case AutoStartControl: top = SettingsPosition(AutoStart); controlHeight = SettingsLayout::RowHeight; break;
        case OpenDataControl: top = SettingsPosition(DataFolder); break;
        case BackupControl: case RestoreControl: top = SettingsPosition(Backup); break;
        default: return;
        }
    }
    if (top - scroll < SettingsClipTop() + 2) Scroll(top - SettingsClipTop() - 2);
    else if (top + controlHeight - scroll > height - 18) Scroll(top + controlHeight - height + 18);
}

bool App::NavigationKey(UINT key) {
    if (contextMenu) return MenuKey(key);
    const bool control = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
    const bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
    if (key == VK_TAB) { Tab(shift); return true; }
    if (dialog == Dialog::None && page == Page::Settings && focused >= SettingsThemes && focused <= SettingsBackup &&
        (key == VK_LEFT || key == VK_RIGHT)) {
        const int next = std::clamp(focused + (key == VK_RIGHT ? 1 : -1), static_cast<int>(SettingsThemes),
                                    static_cast<int>(SettingsBackup));
        SwitchSettings(static_cast<SettingsSection>(next - SettingsThemes));
        return true;
    }
    if (dialog == Dialog::None && page == Page::Settings && (key == VK_PRIOR || key == VK_NEXT)) {
        const float pageHeight = std::max(40.f, height - SettingsClipTop() - 36.f);
        Scroll(scroll + (key == VK_PRIOR ? -pageHeight : pageHeight));
        ChangeHover(0);
        return true;
    }
    if (key == VK_ESCAPE) {
        if (dragging) {
            ChangePress(0);
            EndDrag();
            if (GetCapture() == window) ReleaseCapture();
            Invalidate();
        } else if (dialog == Dialog::Cover) FinishCover(false);
        else if (dialog != Dialog::None) CloseDialog();
        else if (ambientPreview >= 0) {
            ambientPreview = -1;
            ChangePress(0);
            if (GetCapture() == window) ReleaseCapture();
            SyncAmbient();
            Invalidate();
        } else if (expandedSetting) ToggleBehavior(expandedSetting);
        else if (searchExpanded) ShowSearch(false);
        else if (page == Page::Settings) View(Page::Library);
        else { SetFocus(window); focused = 0; Hot(); }
        return true;
    }
    if (control && key == 'F' && dialog == Dialog::None) {
        if (page == Page::Settings) View(Page::Library);
        keyboard = true;
        ShowSearch(true);
        return true;
    }
    if (key == VK_RETURN || (key == VK_SPACE && GetFocus() == window)) {
        keyboard = true;
        if (GetFocus() == nameEdit) Click(SaveEdit);
        else if (GetFocus() == search && !visible.empty()) Click(Tile);
        else if (focused) Click(focused);
        return true;
    }
    if (dialog == Dialog::Cover && focused == CoverPreview && cropPreview.pixels) {
        switch (key) {
        case VK_LEFT: PanCover(8, 0); return true;
        case VK_RIGHT: PanCover(-8, 0); return true;
        case VK_UP: PanCover(0, 8); return true;
        case VK_DOWN: PanCover(0, -8); return true;
        case VK_ADD: case VK_OEM_PLUS: ZoomCover(.1f); return true;
        case VK_SUBTRACT: case VK_OEM_MINUS: ZoomCover(-.1f); return true;
        }
    }
    if (dialog == Dialog::None && page == Page::Settings && focused == AmbientControl) {
        switch (key) {
        case VK_LEFT: case VK_DOWN: SaveAmbient(AmbientValue() - 5); return true;
        case VK_RIGHT: case VK_UP: SaveAmbient(AmbientValue() + 5); return true;
        case VK_HOME: SaveAmbient(0); return true;
        case VK_END: SaveAmbient(AmbientMax); return true;
        }
    }
    if (page == Page::Settings && dialog == Dialog::None && (key == VK_UP || key == VK_DOWN)) {
        if (const auto* menu = BehaviorFor(expandedSetting)) {
            const int count = static_cast<int>(menu->options.size());
            const int previous = focused - FirstBehaviorOption;
            const int next = previous >= 0 && previous < count
                ? std::clamp(previous + (key == VK_DOWN ? 1 : -1), 0, count - 1)
                : BehaviorSelection(menu->id);
            focused = FirstBehaviorOption + next;
            keyboard = true;
            RevealSetting(focused);
            Invalidate();
            return true;
        }
    }
    if (dialog != Dialog::None || page == Page::Settings) return false;
    if (key == VK_APPS || (key == VK_F10 && shift)) {
        keyboard = true;
        const bool onTile = focused >= Tile && selected >= 0 && static_cast<size_t>(selected) < visible.size();
        const auto box = onTile ? GridBox(selected) : D2D1::RectF(24.f, GridTop(), width, height);
        POINT point{ToPixel(box.left + 12), ClientY(std::max(box.top, GridTop()) + 18)};
        ClientToScreen(window, &point);
        Context(onTile ? Tile + selected : 0, point);
        return true;
    }
    if (visible.empty()) return false;
    if (GetFocus() == search && key != VK_DOWN && key != VK_UP) return false;
    int position = selected;
    switch (key) {
    case VK_UP: position -= columns; break;
    case VK_DOWN: position += columns; break;
    case VK_LEFT: --position; break;
    case VK_RIGHT: ++position; break;
    case VK_HOME: position = 0; break;
    case VK_END: position = static_cast<int>(visible.size()) - 1; break;
    case VK_PRIOR: position -= columns * std::max(1, static_cast<int>((height - GridTop()) / RowHeight())); break;
    case VK_NEXT: position += columns * std::max(1, static_cast<int>((height - GridTop()) / RowHeight())); break;
    case VK_F2:
        if (selected >= 0) OpenEditor(library.items[visible[selected]].id);
        return true;
    default: return false;
    }
    keyboard = true;
    if (selected < 0 && key != VK_END) position = 0;
    FocusTile(position);
    return true;
}

void App::AddFiles(std::vector<fs::path> paths) {
    if (paths.empty()) return;
    std::wstring report;
    const bool added = library.Add(paths, report);
    if (added) {
        if (page == Page::Settings) View(Page::Library);
        else Filter();
    }
    if (!report.empty()) Error(report);
}

void App::OpenDialog(Dialog next, const std::wstring& text) {
    CloseMenu();
    dialog = next;
    notice = text;
    focused = next == Dialog::Edit ? EditName :
        next == Dialog::Remove || next == Dialog::Restore ? CancelEdit : DismissError;
    hover = 0;
    Hot();
    if (effects && !minimized) {
        dialogSince = GetTickCount64();
        SetTimer(window, AnimationTimer, 16, nullptr);
    }
    Layout();
    SetFocus(next == Dialog::Edit ? nameEdit : window);
    if (next == Dialog::Edit) SendMessageW(nameEdit, EM_SETSEL, 0, -1);
}

void App::CloseDialog() {
    dialog = Dialog::None;
    dialogSince = 0;
    chosenIcon.clear();
    restorePath.clear();
    resetIcon = false;
    preview.reset();
    previewBitmap.Reset();
    coverDraft.reset();
    cropPreview = {};
    editing.clear();
    SetFocus(window);
    focused = selected >= 0 ? Tile + selected : 0;
    Layout();
    Hot();
}

void App::Error(const std::wstring& text) { OpenDialog(Dialog::Error, text); }

void App::OpenEditor(const std::wstring& id) {
    const auto index = library.Find(id);
    if (index == library.items.size()) return;
    editing = id;
    chosenIcon.clear();
    resetIcon = false;
    preview.reset();
    previewBitmap.Reset();
    SetWindowTextW(nameEdit, library.items[index].name.c_str());
    RequestIcon(library.items[index]);
    OpenDialog(Dialog::Edit);
}

void App::Replace(const std::wstring& id) {
    const auto files = ChooseFiles(window, FileKind::Shortcut, false);
    if (files.empty()) return;
    if (!library.Replace(id, files.front())) Error(Texts::ChangeTargetError.Get());
    else Filter();
}

void App::Record(Preferences preferences) {
    const bool layoutChanged = library.prefs.tileStyle != preferences.tileStyle || library.prefs.labels != preferences.labels ||
        library.prefs.tileSize != preferences.tileSize || library.prefs.tileSpacing != preferences.tileSpacing;
    if (!library.SetPreferences(preferences)) {
        Error(Texts::SaveSettingsError.Get());
        return;
    }
    ApplyTheme();
    if (layoutChanged) Layout();
}

int App::BehaviorSelection(int id) const {
    switch (id) {
    case AfterLaunchControl: return static_cast<int>(library.prefs.afterLaunch);
    case MinimizeControl: return static_cast<int>(library.prefs.onMinimize);
    case CloseControl: return static_cast<int>(library.prefs.onClose);
    case HotkeyControl: return library.prefs.globalHotkey;
    default: return 0;
    }
}

void App::ToggleBehavior(int id) {
    const auto* menu = BehaviorFor(id);
    if (!menu) return;
    const bool opening = expandedSetting != id;
    expandedSetting = opening ? id : 0;
    focused = id;
    Layout();
    if (opening) RevealSetting(FirstBehaviorOption + static_cast<int>(menu->options.size()) - 1);
    RevealSetting(id);
}

void App::Click(int id) {
    if (id < Tile) { Command(id); return; }
    const auto position = static_cast<size_t>(id - Tile);
    if (position >= visible.size()) return;
    selected = static_cast<int>(position);
    focused = id;
    const auto target = library.items[visible[position]].id;
    Launch(target);
}

void App::Command(int id) {
    if (dialog == Dialog::Cover) {
        CoverCommand(id);
        return;
    }
    if (dialog == Dialog::Edit) {
        if (id == EditCover) {
            OpenCover();
            return;
        }
        if (id == RemoveCoverEdit && HasCover()) {
            coverDraft = CoverChange{{}, {}, true};
            focused = EditCover;
            notice.clear();
        } else if (id == ChangeIcon) {
            const auto paths = ChooseFiles(window, FileKind::Icon, false);
            if (paths.empty()) return;
            auto image = DecodeIcon(paths.front(), 256);
            if (!image) {
                notice = Fold(paths.front().extension().wstring()) == L".exe"
                    ? Texts::NoExeIcon.Get()
                    : Texts::ReadIconError.Get();
            } else {
                chosenIcon = paths.front();
                preview = std::move(image);
                resetIcon = false;
                notice.clear();
                previewBitmap.Reset();
            }
        } else if (id == ResetIcon) {
            chosenIcon.clear();
            preview.reset();
            previewBitmap.Reset();
            resetIcon = true;
            notice.clear();
            const size_t index = library.Find(editing);
            if (index < library.items.size()) RequestIcon(library.items[index], true);
        } else if (id == SaveEdit) {
            const auto name = Trim(WindowText(nameEdit));
            if (name.empty()) notice = Texts::EnterName.Get();
            else if (!library.Edit(editing, name, chosenIcon, resetIcon, coverDraft ? &*coverDraft : nullptr, preview.get())) {
                notice = Texts::SaveChangesError.Get();
            } else { CloseDialog(); Filter(); }
        } else if (id == CancelEdit) CloseDialog();
        Invalidate();
        return;
    }
    if (dialog != Dialog::None) {
        const auto target = editing;
        if (id == ConfirmRestore && dialog == Dialog::Restore) {
            const auto path = restorePath;
            CloseDialog();
            if (launching) { Error(Texts::WaitForRestore.Get()); return; }
            SetCursor(LoadCursorW(nullptr, IDC_WAIT));
            const auto restored = library.Restore(path);
            SetCursor(LoadCursorW(nullptr, IDC_ARROW));
            if (restored == OpenResult::Ok) {
                icons.clear();
                requested.clear();
                iconBytes = 0;
                ApplyTheme();
                UpdateHotkey();
                Filter();
                OpenDialog(Dialog::Info, Texts::RestoreSuccess.Get());
            } else if (restored == OpenResult::NewerVersion) Error(Texts::NewerBackupError.Get());
            else Error(Texts::RestoreError.Get());
        } else if (id == ReplaceError) { CloseDialog(); Replace(target); }
        else if (id == ConfirmRemoval) {
            CloseDialog();
            if (library.Remove(target)) Filter();
            else Error(Texts::RemoveShortcutError.Get());
        } else if (id == DismissError || id == CancelEdit) CloseDialog();
        return;
    }
    auto preferences = library.prefs;
    if (id >= FirstLightPalette && id < Tile) {
        if (id >= FirstDarkPalette) preferences.darkPalette = id - FirstDarkPalette;
        else preferences.lightPalette = id - FirstLightPalette;
        Record(preferences);
        return;
    }
    if (const auto* menu = BehaviorFor(expandedSetting); menu && id >= FirstBehaviorOption &&
        id - FirstBehaviorOption < static_cast<int>(menu->options.size())) {
        const int choice = id - FirstBehaviorOption;
        switch (menu->id) {
        case AfterLaunchControl: preferences.afterLaunch = static_cast<AfterLaunch>(choice); break;
        case MinimizeControl: preferences.onMinimize = static_cast<OnMinimize>(choice); break;
        case CloseControl: preferences.onClose = static_cast<OnClose>(choice); break;
        case HotkeyControl: preferences.globalHotkey = choice; break;
        }
        ToggleBehavior(menu->id);
        Record(preferences);
        if (menu->id == HotkeyControl && !UpdateHotkey()) {
            preferences.globalHotkey = 0;
            Record(preferences);
            Error(Texts::HotkeyTaken.Get());
        }
        return;
    }
    switch (id) {
    case ScrollUp: case ScrollDown: {
        const float top = page == Page::Settings ? SettingsClipTop() : GridTop();
        const float step = std::max(40.f, (height - top - 20.f) * .75f);
        Scroll(scroll + (id == ScrollUp ? -step : step));
        ChangeHover(0);
        break;
    }
    case Search: ShowSearch(!searchExpanded); break;
    case Add: AddFiles(ChooseFiles(window, FileKind::Shortcut, true)); break;
    case Settings: View(Page::Settings); break;
    case Back: View(Page::Library); break;
    case SettingsThemes: case SettingsTiles: case SettingsBehavior: case SettingsBackup:
        SwitchSettings(static_cast<SettingsSection>(id - SettingsThemes)); break;
    case BrowseLight: case BrowseDark: BrowsePalettes(id == BrowseDark); break;
    case ThemeSystem:
    case ThemeLight:
    case ThemeDark:
        preferences.theme = id - ThemeSystem;
        Record(preferences);
        if (dialog == Dialog::None) {
            BrowsePalettes(dark);
            focused = id;
        }
        break;
    case TilesCentered:
    case TilesCover:
    case TilesCompact: preferences.tileStyle = static_cast<TileStyle>(id - TilesCentered); Record(preferences); break;
    case LabelsAlways: case LabelsHover: case LabelsHidden:
        preferences.labels = static_cast<TileLabels>(id - LabelsAlways); Record(preferences); break;
    case SizeSmall: case SizeMedium: case SizeLarge:
        preferences.tileSize = static_cast<TileSize>(id - SizeSmall); Record(preferences); break;
    case SpacingTight: case SpacingNormal: case SpacingWide:
        preferences.tileSpacing = static_cast<TileSpacing>(id - SpacingTight); Record(preferences); break;
    case TileHalo: preferences.tileHalo = !preferences.tileHalo; Record(preferences); break;
    case Motion: preferences.motion = !preferences.motion; Record(preferences); break;
    case MotionOnBattery:
        if (portableBattery) {
            preferences.pauseMotionOnBattery = !preferences.pauseMotionOnBattery;
            Record(preferences);
        }
        break;
    case AfterLaunchControl: case MinimizeControl: case CloseControl: case HotkeyControl: ToggleBehavior(id); break;
    case OpenDataControl:
        if (LaunchShortcut(library.root, window) != LaunchResult::Opened) Error(Texts::OpenLibraryError.Get());
        break;
    case AutoStartControl:
        if (SetAutoStart(!autoStart)) {
            autoStart = AutoStartRegistered();
            Invalidate();
        } else Error(Texts::AutostartError.Get());
        break;
    case BackupControl: {
        const auto path = ChooseBackupDestination(window);
        if (path.empty()) break;
        SetCursor(LoadCursorW(nullptr, IDC_WAIT));
        const bool saved = library.Backup(path);
        SetCursor(LoadCursorW(nullptr, IDC_ARROW));
        if (saved) OpenDialog(Dialog::Info, Texts::BackupSuccess.Get());
        else Error(Texts::BackupError.Get());
        break;
    }
    case RestoreControl: {
        const auto paths = ChooseFiles(window, FileKind::Backup, false);
        if (!paths.empty()) {
            restorePath = paths.front();
            OpenDialog(Dialog::Restore, Texts::RestoreConfirmation.Get());
        }
        break;
    }
    default: break;
    }
}

int App::TitleAt(POINT client) const {
    const float x = ToDip(client.x), y = ToDip(client.y);
    if (y < 0 || y >= TitleHeight) return 0;
    for (int id = CaptionMinimize; id <= CaptionClose; ++id) {
        if (Inside(TitleButton(id), x, y)) return id;
    }
    return 0;
}

void App::ChangeTitleHover(int id) {
    if (titleHover == id) return;
    AnimateControl(hoverMotion, titleHover, id);
    titleHover = id;
    Invalidate();
}

void App::ChangeTitlePress(int id) {
    if (titlePress == id) return;
    AnimateControl(pressMotion, titlePress, id);
    titlePress = id;
    Invalidate();
}

void App::RunTitleButton(int id) {
    switch (id) {
    case CaptionMinimize:
        captionMinimize = true;
        SendMessageW(window, WM_SYSCOMMAND, SC_MINIMIZE, 0);
        captionMinimize = false;
        break;
    case CaptionMaximize: SendMessageW(window, WM_SYSCOMMAND, IsZoomed(window) ? SC_RESTORE : SC_MAXIMIZE, 0); break;
    case CaptionClose: SendMessageW(window, WM_SYSCOMMAND, SC_CLOSE, 0); break;
    }
}

LRESULT App::Handle(UINT message, WPARAM w, LPARAM l) {
    if (message == taskbarCreated && taskbarCreated && inTray) {
        if (!TrayIconRegistered(window) && !AddTray()) {
            trayRetries = 0;
            SetTimer(window, StartupTrayTimer, 500, nullptr);
        }
        return 0;
    }
    switch (message) {
    case WM_NCCALCSIZE: {
        auto& client = w ? reinterpret_cast<NCCALCSIZE_PARAMS*>(l)->rgrc[0] : *reinterpret_cast<RECT*>(l);
        const LONG windowTop = client.top;
        DefWindowProcW(window, message, w, l);
        if (IsZoomed(window)) {
            // Keep maximized content inside the monitor's work area.
            const LONG frame = ResizeInset(SM_CYSIZEFRAME, GetDpiForWindow(window));
            client.top = std::min(client.top, windowTop + frame);
        } else if (!IsIconic(window)) {
            client.top = windowTop;
        }
        return 0;
    }
    case WM_CREATE: return Create(window) ? 0 : -1;
    case WM_ACTIVATE:
        activeWindow = LOWORD(w) != WA_INACTIVE;
        if (!activeWindow) {
            tileImageFadeUntil = 0;
            CloseMenu();
            ChangeTitleHover(0);
            ChangeTitlePress(0);
        }
        Invalidate();
        SyncAmbient();
        break;
    case WM_PAINT: Paint(); return 0;
    case WM_ERASEBKGND: return 1;
    case WM_GETMINMAXINFO: {
        auto* info = reinterpret_cast<MINMAXINFO*>(l);
        info->ptMinTrackSize = {ToPixel(MinimumWindowWidth), ToPixel(MinimumWindowHeight)};
        return 0;
    }
    case WM_SIZE:
        Suspend(w == SIZE_MINIMIZED || inTray);
        if (w == SIZE_RESTORED || w == SIZE_MAXIMIZED) CaptureWindowSize();
        if (!minimized && search) {
            canvas.Size(window, dpi);
            Layout();
        }
        return 0;
    case WM_EXITSIZEMOVE:
        CaptureWindowSize();
        SaveWindowSize();
        return 0;
    case WM_DPICHANGED: {
        CloseMenu();
        dpi = LOWORD(w);
        const auto* box = reinterpret_cast<RECT*>(l);
        SetWindowPos(window, nullptr, box->left, box->top, box->right - box->left, box->bottom - box->top,
                     SWP_NOACTIVATE | SWP_NOZORDER);
        CaptureWindowSize();
        UpdateFonts();
        canvas.Size(window, dpi);
        previewBitmap.Reset();
        Layout();
        return 0;
    }
    case WM_THEMECHANGED:
    case WM_SETTINGCHANGE:
    case WM_SYSCOLORCHANGE: if (search) ApplyTheme(); break;
    case WM_POWERBROADCAST:
        if (w == PBT_APMPOWERSTATUSCHANGE || w == PBT_APMRESUMEAUTOMATIC) {
            const bool wasPortable = portableBattery, wasOnBattery = onBattery;
            RefreshPowerStatus();
            if (!portableBattery && focused == MotionOnBattery) focused = Motion;
            if (wasPortable != portableBattery || wasOnBattery != onBattery) {
                UpdateMotion();
                Invalidate();
            }
            return TRUE;
        }
        break;
    case WM_COMMAND:
        if (LOWORD(w) == EditSearchId && HIWORD(w) == EN_CHANGE) {
            query = WindowText(search);
            scroll = 0;
            Filter();
        } else if (HIWORD(w) == EN_SETFOCUS || HIWORD(w) == EN_KILLFOCUS) {
            InvalidateRect(reinterpret_cast<HWND>(l), nullptr, FALSE);
            Invalidate();
        }
        return 0;
    case WM_CTLCOLOREDIT: {
        auto device = reinterpret_cast<HDC>(w);
        const auto text = theme.text, field = theme.field;
        SetTextColor(device, RGB(text.r * 255, text.g * 255, text.b * 255));
        SetBkColor(device, RGB(field.r * 255, field.g * 255, field.b * 255));
        return reinterpret_cast<LRESULT>(fieldBrush);
    }
    case WM_KEYDOWN: if (NavigationKey(static_cast<UINT>(w))) return 0; break;
    case WM_TIMER:
        if (w == AnimationTimer) { MotionTick(); return 0; }
        if (w == AmbientTimer) { if (ambientSince) Invalidate(); return 0; }
        if (w == DragTimer) {
            if (dragging) { Scroll(scroll + dragStep); UpdateDrag(); }
            return 0;
        }
        if (w == StartupTrayTimer) {
            const bool registered = inTray ? TrayIconRegistered(window) || AddTray() : SendToTray();
            if (registered) KillTimer(window, StartupTrayTimer);
            else if (++trayRetries >= 10) {
                if (inTray) Restore();
                else DestroyWindow(window);
            }
            return 0;
        }
        break;
    case WM_NCHITTEST: {
        const auto hit = DefWindowProcW(window, message, w, l);
        if (hit != HTCLIENT) return hit;
        POINT client{GET_X_LPARAM(l), GET_Y_LPARAM(l)};
        ScreenToClient(window, &client);
        const UINT currentDpi = GetDpiForWindow(window);
        if (!IsZoomed(window) && client.y >= 0 && client.y < ResizeInset(SM_CYSIZEFRAME, currentDpi)) {
            const int corner = ResizeInset(SM_CXSIZEFRAME, currentDpi);
            if (client.x < corner) return HTTOPLEFT;
            if (client.x >= ToPixel(width) - corner) return HTTOPRIGHT;
            return HTTOP;
        }
        if (ContentY(client.y) >= 0) return HTCLIENT;
        switch (TitleAt(client)) {
        case CaptionMinimize: return HTMINBUTTON;
        case CaptionMaximize: return HTMAXBUTTON;
        case CaptionClose: return HTCLOSE;
        default: return HTCAPTION;
        }
    }
    case WM_NCMOUSEMOVE: {
        if (!titleTracking) {
            TRACKMOUSEEVENT event{sizeof(event), TME_LEAVE | TME_NONCLIENT, window, 0};
            titleTracking = TrackMouseEvent(&event) != FALSE;
        }
        POINT client{GET_X_LPARAM(l), GET_Y_LPARAM(l)};
        ScreenToClient(window, &client);
        ChangeTitleHover(w == HTMINBUTTON || w == HTMAXBUTTON || w == HTCLOSE ? TitleAt(client) : 0);
        break;
    }
    case WM_NCMOUSELEAVE: titleTracking = false; ChangeTitleHover(0); break;
    case WM_NCLBUTTONDBLCLK:
        if (w == HTCAPTION) { RunTitleButton(CaptionMaximize); return 0; }
        break;
    case WM_NCLBUTTONDOWN: {
        CloseMenu();
        if (w == HTMINBUTTON || w == HTMAXBUTTON || w == HTCLOSE) {
            const int id = w == HTMINBUTTON ? CaptionMinimize : w == HTMAXBUTTON ? CaptionMaximize : CaptionClose;
            if (GetForegroundWindow() != window) SetForegroundWindow(window);
            ChangeTitlePress(id);
            SetCapture(window);
            return 0;
        }
        break;
    }
    case WM_SYSCOMMAND:
        if ((w & 0xFFF0) == SC_MINIMIZE && library.prefs.onMinimize == OnMinimize::Tray) {
            if (captionMinimize && SendToTray()) return 0;
        }
        break;
    case WM_MOUSEMOVE: {
        const int x = GET_X_LPARAM(l), y = GET_Y_LPARAM(l);
        if (titlePress) {
            ChangeTitleHover(TitleAt({x, y}));
            return 0;
        }
        ChangeTitleHover(0);
        if (!tracking) {
            TRACKMOUSEEVENT event{sizeof(event), TME_LEAVE, window, 0};
            tracking = TrackMouseEvent(&event) != FALSE;
        }
        if (pressed == CoverPreview) PanCover(ToDip(x - pointer.x), ToDip(y - pointer.y));
        else if (pressed == AmbientControl && ambientPreview >= 0) PreviewAmbient(ToDip(x));
        pointer = {x, y};
        keyboard = false;
        const int id = At(ToDip(x), ContentY(y));
        if (pressed >= Tile && dialog == Dialog::None) {
            if (!dragging && (std::abs(x - down.x) > ToPixel(6) || std::abs(y - down.y) > ToPixel(6))) dragging = true;
            if (dragging) {
                SetCursor(LoadCursorW(nullptr, IDC_SIZEALL));
                UpdateDrag();
            }
        }
        ChangeHover(dragging ? 0 : id);
        return 0;
    }
    case WM_MOUSELEAVE: tracking = false; ChangeHover(0); return 0;
    case WM_SETCURSOR:
        if (LOWORD(l) == HTCLIENT && (dragging || hover || pressed == CoverPreview)) {
            SetCursor(LoadCursorW(nullptr, dragging || pressed == CoverPreview || hover == CoverPreview ? IDC_SIZEALL : IDC_HAND));
            return TRUE;
        }
        break;
    case WM_LBUTTONDOWN:
        down = pointer = {GET_X_LPARAM(l), GET_Y_LPARAM(l)};
        keyboard = false;
        ChangePress(At(ToDip(down.x), ContentY(down.y)));
        if (contextMenu && !MenuCommand(pressed)) {
            if (!Inside(MenuBox(), ToDip(down.x), ContentY(down.y))) CloseMenu();
            return 0;
        }
        if (pressed) {
            SetCapture(window);
            if (pressed != EditName) SetFocus(window);
            if (pressed == CoverPreview || pressed == AmbientControl) focused = pressed;
            if (pressed == AmbientControl) PreviewAmbient(ToDip(down.x));
            Invalidate();
        }
        return 0;
    case WM_LBUTTONUP: {
        if (titlePress) {
            const int id = titlePress;
            const bool clicked = TitleAt({GET_X_LPARAM(l), GET_Y_LPARAM(l)}) == id;
            ChangeTitlePress(0);
            if (GetCapture() == window) ReleaseCapture();
            if (clicked) RunTitleButton(id);
            return 0;
        }
        const bool wasDragging = dragging;
        const float x = ToDip(GET_X_LPARAM(l)), y = ContentY(GET_Y_LPARAM(l));
        const int id = wasDragging ? DropAt(x, y) : At(x, y);
        const int previous = pressed;
        const bool adjustingAmbient = previous == AmbientControl && ambientPreview >= 0;
        ChangePress(0);
        EndDrag();
        if (GetCapture() == window) ReleaseCapture();
        if (adjustingAmbient) {
            PreviewAmbient(x);
            SaveAmbient(ambientPreview);
            return 0;
        }
        if (wasDragging && id >= Tile && previous >= Tile && id != previous &&
            static_cast<size_t>(std::max(id, previous) - Tile) < visible.size()) {
            const auto& origin = library.items[visible[previous - Tile]].id;
            const auto& target = library.items[visible[id - Tile]].id;
            if (library.Move(origin, target)) Filter();
            else Error(Texts::SaveOrderError.Get());
        } else if (!wasDragging && previous && previous == id) {
            if (MenuCommand(id)) RunMenu(id);
            else if (id == EditName) SetFocus(nameEdit);
            else Click(id);
        }
        Invalidate();
        return 0;
    }
    case WM_CAPTURECHANGED:
        ChangeTitlePress(0);
        ChangePress(0);
        EndDrag();
        if (ambientPreview >= 0) { ambientPreview = -1; SyncAmbient(); }
        Invalidate();
        return 0;
    case WM_CONTEXTMENU:
        if (dialog == Dialog::None && page != Page::Settings) {
            POINT point{GET_X_LPARAM(l), GET_Y_LPARAM(l)};
            if (point.x == -1 && point.y == -1) { NavigationKey(VK_APPS); return 0; }
            POINT client = point;
            ScreenToClient(window, &client);
            if (!Inside(D2D1::RectF(0, 0, width, height), ToDip(client.x), ContentY(client.y))) {
                CloseMenu();
                break;
            }
            CloseMenu();
            keyboard = false;
            const int id = At(ToDip(client.x), ContentY(client.y));
            if (!id || id >= Tile) Context(id, point);
        }
        return 0;
    case WM_MOUSEWHEEL:
        if (contextMenu) return 0;
        if (dialog == Dialog::Cover) {
            POINT point{GET_X_LPARAM(l), GET_Y_LPARAM(l)};
            ScreenToClient(window, &point);
            if (Inside(CoverViewport(), ToDip(point.x), ContentY(point.y))) {
                ZoomCover(GET_WHEEL_DELTA_WPARAM(w) * (.12f / WHEEL_DELTA));
            }
            return 0;
        }
        if (dialog == Dialog::None) {
            Scroll(scroll - GET_WHEEL_DELTA_WPARAM(w) * .58f);
            if (page == Page::Settings) { ChangeHover(0); return 0; }
            if (dragging) { UpdateDrag(); return 0; }
            POINT point{GET_X_LPARAM(l), GET_Y_LPARAM(l)};
            ScreenToClient(window, &point);
            const float x = ToDip(point.x), y = ContentY(point.y);
            const int tile = DropAt(x, y);
            ChangeHover(tile && Inside(GridBox(tile - Tile), x, y) ? tile : 0);
        }
        return 0;
    case WM_DROPFILES: {
        const auto drop = reinterpret_cast<HDROP>(w);
        const UINT count = DragQueryFileW(drop, 0xFFFFFFFF, nullptr, 0);
        std::vector<fs::path> paths;
        for (UINT i = 0; i < count; ++i) {
            const auto length = DragQueryFileW(drop, i, nullptr, 0);
            std::wstring path(length + 1, L'\0');
            path.resize(DragQueryFileW(drop, i, path.data(), length + 1));
            if (!path.empty()) paths.emplace_back(path);
        }
        DragFinish(drop);
        CloseMenu();
        if (dialog == Dialog::None) AddFiles(std::move(paths));
        else if (dialog == Dialog::Cover && !paths.empty()) SetCoverImage(paths.front());
        return 0;
    }
    case IconReady: {
        std::unique_ptr<IconResult> result(reinterpret_cast<IconResult*>(l));
        StoreIcon(std::move(*result));
        if (!minimized) Invalidate();
        return 0;
    }
    case LoadVisibleIcons: LoadIcons(); return 0;
    case LaunchReady: {
        std::unique_ptr<std::wstring> id(reinterpret_cast<std::wstring*>(l));
        launching = false;
        const auto result = static_cast<LaunchResult>(w);
        if (result == LaunchResult::Cancelled) return 0;
        if (result == LaunchResult::Opened) {
            if (!library.RecordLaunch(*id)) Log(L"Couldn't record a launch");
            if (library.prefs.afterLaunch == AfterLaunch::Exit) DestroyWindow(window);
            else if (library.prefs.afterLaunch == AfterLaunch::Tray && !SendToTray()) ShowWindow(window, SW_MINIMIZE);
        } else {
            editing = *id;
            OpenDialog(Dialog::Missing, Texts::LaunchError.Get());
        }
        return 0;
    }
    case FocusExisting:
        Restore();
        return 0;
    case TrayCallback:
        if (LOWORD(l) == NIN_SELECT || LOWORD(l) == NIN_KEYSELECT || LOWORD(l) == WM_LBUTTONDBLCLK) Restore();
        else if (LOWORD(l) == WM_CONTEXTMENU) TrayMenu({GET_X_LPARAM(w), GET_Y_LPARAM(w)});
        return 0;
    case WM_CLOSE:
        if (library.prefs.onClose == OnClose::Tray) {
            if (!SendToTray()) ShowWindow(window, SW_MINIMIZE);
        } else DestroyWindow(window);
        return 0;
    case WM_HOTKEY:
        if (w == ShowWindowHotkey) ToggleFromHotkey();
        return 0;
    case WM_ENDSESSION:
        if (w) SaveWindowSize();
        return 0;
    case WM_DESTROY: {
        SaveWindowSize();
        RemoveTray();
        if (hotkeyRegistered) UnregisterHotKey(window, ShowWindowHotkey);
        StopIconWorker();
        MSG queued{};
        while (PeekMessageW(&queued, window, IconReady, IconReady, PM_REMOVE)) {
            delete reinterpret_cast<IconResult*>(queued.lParam);
        }
        PostQuitMessage(0);
        return 0;
    }
    default: break;
    }
    return DefWindowProcW(window, message, w, l);
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR commandLine, int show) {
    const HRESULT com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    if (FAILED(com)) return 1;
    INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_STANDARD_CLASSES};
    InitCommonControlsEx(&controls);
    const bool startup = Trim(commandLine) == L"--startup";
    const auto root = DataPath();
    InitLog(root);
    auto mutexName = std::wstring(L"Local\\Polytsia-") + std::to_wstring(PathHash(Fold(root.wstring())));
    const HANDLE mutex = CreateMutexW(nullptr, FALSE, mutexName.c_str());
    const DWORD mutexError = GetLastError();
    if (!mutex) { CoUninitialize(); return 1; }
    if (mutexError == ERROR_ALREADY_EXISTS) {
        HWND existing = nullptr;
        for (int attempt = 0; !startup && attempt < 20 && !existing; ++attempt) {
            existing = FindWindowW(WindowClass, mutexName.c_str());
            if (!existing) {
                HWND candidate = nullptr;
                while ((candidate = FindWindowExW(nullptr, candidate, WindowClass, nullptr)) != nullptr) {
                    if (GetPropW(candidate, mutexName.c_str()) == reinterpret_cast<HANDLE>(1)) {
                        existing = candidate;
                        break;
                    }
                }
            }
            if (!existing) Sleep(15);
        }
        if (existing) {
            DWORD process = 0;
            GetWindowThreadProcessId(existing, &process);
            AllowSetForegroundWindow(process);
            PostMessageW(existing, FocusExisting, 0, 0);
        }
        CloseHandle(mutex);
        CoUninitialize();
        return 0;
    }

    int result = 1;
    BufferedPaintInit();
    {
        App app;
        app.instance = instance;
        const auto opened = root.empty() ? OpenResult::Failed : app.library.Open(root);
        if (opened != OpenResult::Ok) {
            const auto* text = opened == OpenResult::NewerVersion ? Texts::NewerLibraryError.Get() : Texts::OpenLibraryError.Get();
            if (!startup) MessageBoxW(nullptr, text, Texts::AppName.Get(), MB_OK | MB_ICONERROR);
        } else {
            const auto icon = LoadIconW(instance, MAKEINTRESOURCEW(1));
            WNDCLASSEXW registration{sizeof(registration)};
            registration.style = CS_HREDRAW | CS_VREDRAW;
            registration.lpfnWndProc = WindowProc;
            registration.hInstance = instance;
            registration.hIcon = registration.hIconSm = icon;
            registration.hCursor = LoadCursorW(nullptr, IDC_ARROW);
            registration.lpszClassName = WindowClass;
            RegisterClassExW(&registration);
            POINT pointer{};
            GetCursorPos(&pointer);
            MONITORINFO monitor{sizeof(monitor)};
            if (!GetMonitorInfoW(MonitorFromPoint(pointer, MONITOR_DEFAULTTOPRIMARY), &monitor)) {
                monitor.rcWork = {0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN)};
            }
            app.windowSize = app.library.windowSize;
            auto fittedRect = [&](UINT dpi) {
                const auto work = monitor.rcWork;
                const LONG width = std::min<LONG>(MulDiv(std::max(MinimumWindowWidth, app.windowSize.width), dpi, 96),
                                                  work.right - work.left);
                const LONG height = std::min<LONG>(MulDiv(std::max(MinimumWindowHeight, app.windowSize.height), dpi, 96),
                                                   work.bottom - work.top);
                const LONG left = (work.right + work.left - width) / 2, top = (work.bottom + work.top - height) / 2;
                return RECT{left, top, left + width, top + height};
            };
            const UINT systemDpi = GetDpiForSystem();
            const auto bounds = fittedRect(systemDpi);
            constexpr DWORD style = WS_THICKFRAME | WS_SYSMENU | WS_MINIMIZEBOX | WS_MAXIMIZEBOX | WS_CLIPCHILDREN;
            HWND window = CreateWindowExW(WS_EX_APPWINDOW, WindowClass, mutexName.c_str(), style,
                                          bounds.left, bounds.top, bounds.right - bounds.left, bounds.bottom - bounds.top,
                                          nullptr, nullptr, instance, &app);
            if (window) {
                // Creating an overlapped window can add WS_CAPTION to the requested style.
                const auto actualStyle = GetWindowLongPtrW(window, GWL_STYLE);
                SetWindowLongPtrW(window, GWL_STYLE, actualStyle & ~static_cast<LONG_PTR>(WS_CAPTION));
                constexpr UINT frameFlags = SWP_NOACTIVATE | SWP_NOZORDER | SWP_FRAMECHANGED;
                const UINT windowDpi = GetDpiForWindow(window);
                if (windowDpi && windowDpi != systemDpi) {
                    const auto fitted = fittedRect(windowDpi);
                    SetWindowPos(window, nullptr, fitted.left, fitted.top, fitted.right - fitted.left, fitted.bottom - fitted.top,
                                 frameFlags);
                } else {
                    SetWindowPos(window, nullptr, 0, 0, 0, 0, frameFlags | SWP_NOMOVE | SWP_NOSIZE);
                }
                SetPropW(window, mutexName.c_str(), reinterpret_cast<HANDLE>(1));
                SetWindowTextW(window, Texts::AppName.Get());
                if (startup) {
                    app.Suspend(true);
                    if (!app.SendToTray()) SetTimer(window, StartupTrayTimer, 500, nullptr);
                } else {
                    app.ShowInitialWindow(show);
                    UpdateWindow(window);
                }
                MSG msg{};
                BOOL pending;
                while ((pending = GetMessageW(&msg, nullptr, 0, 0)) > 0) {
                    TranslateMessage(&msg);
                    DispatchMessageW(&msg);
                }
                result = pending == -1 ? 1 : static_cast<int>(msg.wParam);
            }
        }
    }
    CloseHandle(mutex);
    BufferedPaintUnInit();
    CoUninitialize();
    return result;
}
