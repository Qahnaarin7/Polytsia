#pragma once
#include "appearance.hpp"
#include "graphics.hpp"
#include <windowsx.h>
#include <map>
#include <optional>
#include <set>
#include <span>

constexpr UINT IconReady = WM_APP + 1;
constexpr UINT LaunchReady = WM_APP + 2;
constexpr UINT FocusExisting = WM_APP + 3;
constexpr UINT LoadVisibleIcons = WM_APP + 4;
constexpr UINT TrayCallback = WM_APP + 5;
constexpr int ShowWindowHotkey = 1;

inline constexpr UINT_PTR AnimationTimer = 1;
inline constexpr UINT_PTR StartupTrayTimer = 2;
inline constexpr UINT_PTR AmbientTimer = 3;
inline constexpr UINT_PTR DragTimer = 4;

enum class Page { Library, Settings };
enum class SettingsSection { Themes, Tiles, Behavior, Backup };
enum Control {
    Search = 1, Add, Settings, Back, ThemeSystem, ThemeLight, ThemeDark,
    TilesCentered, TilesCover, TilesCompact, Motion,
    AfterLaunchControl, MinimizeControl, CloseControl, AutoStartControl,
    EditName, ChangeIcon, ResetIcon, CancelEdit, SaveEdit, DismissError, ReplaceError, ConfirmRemoval,
    AmbientControl, EditCover, CoverPreview, ChooseCover, RemoveCover, ResetCrop, ZoomOut, ZoomIn, CancelCover, ApplyCover,
    LabelsAlways, LabelsHover, LabelsHidden, BackupControl, RestoreControl, ConfirmRestore, RemoveCoverEdit,
    SettingsThemes, SettingsTiles, SettingsBehavior, SettingsBackup, BrowseLight, BrowseDark,
    SizeSmall, SizeMedium, SizeLarge, SpacingTight, SpacingNormal, SpacingWide, TileHalo, MotionOnBattery,
    ScrollUp, ScrollDown, HotkeyControl, OpenDataControl,
    FirstBehaviorOption = 60,
    LaunchMenu = 70, AdminMenu, LocationMenu, EditMenu, ReplaceMenu, EarlierMenu, LaterMenu, DeleteMenuItem,
    SearchMenu, AddMenu, SortMenu, SettingsMenu,
    CaptionMinimize = 85, CaptionMaximize, CaptionClose,
    FirstAmbientStyle = 90,
    FirstLightPalette = 100,
    FirstDarkPalette = FirstLightPalette + PaletteCount,
    Tile = FirstDarkPalette + PaletteCount
};
static_assert(OpenDataControl < FirstBehaviorOption);
static_assert(SettingsMenu < CaptionMinimize);
static_assert(FirstAmbientStyle + AmbientStyleCount <= FirstLightPalette);
enum class Dialog { None, Edit, Cover, Error, Missing, Remove, Restore, Info };

struct ContextMenu {
    std::wstring entry;
    POINT anchor;
    ULONGLONG opened;
    bool earlier, later;
    bool elevatable = false, located = false;
};

struct MenuItem {
    int id;
    UiText label;
    bool separator = false;
};

inline constexpr MenuItem TileMenuItems[] = {
    {LaunchMenu, Texts::Open}, {AdminMenu, Texts::RunAsAdmin}, {LocationMenu, Texts::OpenLocation},
    {EditMenu, Texts::EditShortcut, true}, {ReplaceMenu, Texts::ChangeTarget},
    {EarlierMenu, Texts::MoveEarlier, true}, {LaterMenu, Texts::MoveLater},
    {DeleteMenuItem, Texts::RemoveShortcut, true}
};
inline constexpr MenuItem BackgroundMenuItems[] = {
    {SearchMenu, Texts::Search}, {AddMenu, Texts::AddShortcuts},
    {SortMenu, Texts::SortByUse}, {SettingsMenu, Texts::Settings, true}
};

inline constexpr ULONGLONG MenuDuration = 175;
inline constexpr float MenuWidth = 284.f, MenuRow = 34.f, MenuGap = 8.f, MenuPadding = 6.f;

inline bool MenuCommand(int id) { return id >= LaunchMenu && id <= SettingsMenu; }

struct Palette {
    D2D1_COLOR_F background, text, muted, tile, line, field, button, accent, primary, primaryText, dialog, dim;
};
Palette MakePalette(const ColorScheme& colors, bool dark, bool contrast);

struct Hit {
    int id;
    D2D1_RECT_F box;
};

struct IconCache {
    std::shared_ptr<Pixels> pixels;
    ComPtr<ID2D1Bitmap> image;
    ULONGLONG lastUsed{};
    ComPtr<ID2D1Bitmap> backdrop;
    ULONGLONG appeared{};
};

inline constexpr ULONGLONG ControlDuration = 145, TileHoverDuration = 260, TileImageFadeDuration = 165;

struct Tween {
    float from, to;
    ULONGLONG since;
    ULONGLONG duration = ControlDuration;
    float Value(ULONGLONG now) const;
};

class App {
public:
    HWND window{}, search{}, nameEdit{};
    HINSTANCE instance{};
    Canvas canvas;
    Library library;
    WindowSize windowSize;
    Palette theme{};
    const ColorScheme* scheme = &DarkSchemes[0];
    Page page = Page::Library;
    SettingsSection settingsSection = SettingsSection::Themes;
    bool browseDark = true, portableBattery = false, onBattery = false;
    Dialog dialog = Dialog::None;
    bool dark = true, contrast = false, effects = true, minimized = false, glass = false, inTray = false;
    bool keyboard = false, dragging = false, tracking = false, launching = false, iconsPosted = false;
    bool autoStart = false, searchExpanded = false, activeWindow = false, captionMinimize = false, windowShown = false;
    bool titleTracking = false, hotkeyRegistered = false;
    int trayRetries = 0, expandedSetting = 0;
    float width = 0, height = 0, dpi = 96, scroll = 0, maxScroll = 0, tileWidth = 0, gridLeft = 24.f;
    static constexpr float TitleHeight = 36.f;
    float GridTop() const { return searchExpanded ? 80.f : 24.f; }
    int columns = 4, focused = 0, hover = 0, pressed = 0, dragTarget = 0, dragStep = 0;
    int titleHover = 0, titlePress = 0;
    int selected = -1;
    POINT down{}, pointer{};
    ULONGLONG dialogSince = 0, tileImageFadeUntil = 0;
    ULONGLONG ambientSince = 0, ambientElapsed = 0;
    std::wstring query, editing, notice, hotId;
    std::optional<ContextMenu> contextMenu;
    fs::path chosenIcon, restorePath;
    bool resetIcon = false;
    std::shared_ptr<Pixels> preview;
    ComPtr<ID2D1Bitmap> previewBitmap;
    std::optional<CoverChange> coverDraft;
    CoverChange coverWork;
    IconCache cropPreview;
    int ambientPreview = -1;
    D2D1_POINT_2F ambientFocus{-1.f, -1.f};
    std::vector<size_t> visible;
    std::vector<Hit> hits;
    std::map<std::wstring, IconCache> icons;
    size_t iconBytes = 0;
    std::set<std::wstring> requested;
    std::map<std::wstring, Tween> transitions;
    std::map<int, Tween> hoverMotion, pressMotion;
    HBRUSH fieldBrush{};
    HFONT font{};
    UINT taskbarCreated{};

    ~App();
    bool Create(HWND hwnd);
    LRESULT Handle(UINT message, WPARAM wParam, LPARAM lParam);
    void Layout();
    void ApplyTheme();
    void RefreshPowerStatus();
    void UpdateMotion();
    void View(Page next);
    void Filter();
    void Invalidate();
    void Paint();
    void DrawRoot();
    void DrawAmbient(D2D1_RECT_F full);
    void DrawTitle();
    void DrawBrandMark(float left, float top, float size, D2D1_COLOR_F color);
    D2D1_RECT_F TitleButton(int id) const;
    int TitleAt(POINT client) const;
    void ChangeTitleHover(int id);
    void ChangeTitlePress(int id);
    void RunTitleButton(int id);
    void DrawLibrary();
    void DrawScrollArrows(D2D1_RECT_F viewport, float left);
    void DrawTileHalos(D2D1_RECT_F windowBounds);
    void DrawSettings();
    void DrawThemePreview(D2D1_RECT_F panel);
    void DrawDialog();
    void DrawContextMenu();
    void DrawCoverDialog(D2D1_RECT_F panel);
    void DrawTile(int id, D2D1_RECT_F rectangle);
    bool DrawTilePicture(const Shortcut& entry, IconCache* displayed, bool cover, D2D1_RECT_F picture,
                         D2D1_RECT_F tile, const Palette& colors, bool darkTheme, float opacity = 1.f);
    float TileHover(const Shortcut& entry) const;
    IconCache* TileCover(const Shortcut& entry);
    void DrawDragPreview();
    void DrawButton(int id, D2D1_RECT_F rectangle, const wchar_t* text, bool primary = false);
    void DrawPaletteButton(int id, D2D1_RECT_F rectangle, const ColorScheme& option, bool chosen);
    void DrawPaletteSwatch(D2D1_RECT_F rectangle, const ColorScheme& option);
    void DrawIcon(IconCache* icon, D2D1_RECT_F rectangle, float opacity = 1.f);
    float TileImageOpacity(const IconCache* icon) const;
    ID2D1Bitmap* IconBitmap(IconCache* icon);
    void Glyph(int kind, float x, float y, D2D1_COLOR_F color);
    void Place(HWND edit, D2D1_RECT_F rectangle);
    D2D1_RECT_F GridBox(int position) const;
    static constexpr float TileCaptionHeight = 34.f;
    float TileScale() const;
    float TileGap() const;
    float TileHeight() const;
    float RowHeight() const { return TileHeight() + TileGap(); }
    static constexpr size_t ThemeSampleCount = 3;
    D2D1_RECT_F Popup() const;
    D2D1_RECT_F MenuBox() const;
    D2D1_RECT_F MenuItemBox(int id) const;
    D2D1_RECT_F SearchBox() const;
    D2D1_RECT_F CoverViewport() const;
    D2D1_RECT_F AmbientTrack() const;
    int At(float x, float y) const;
    int DropAt(float x, float y) const;
    bool NavigationKey(UINT key);
    void Tab(bool back);
    void RevealSetting(int id);
    int PaletteColumns() const;
    float PaletteOptionTop(int index) const;
    float SettingsEdge() const;
    float SettingsClipTop() const;
    float SettingsBottom() const;
    float AmbientPosition() const;
    float EffectPosition() const;
    float SettingsPosition(float y) const;
    void SwitchSettings(SettingsSection section);
    void BrowsePalettes(bool darkPalette);
    void FocusTile(int position);
    void ChangeHover(int id);
    void ChangePress(int id);
    void AnimateControl(std::map<int, Tween>& animations, int previous, int next);
    float ControlAmount(const std::map<int, Tween>& animations, int id, bool current) const;
    void Hot();
    void MotionTick();
    void SyncAmbient();
    ULONGLONG AmbientTime() const { return ambientElapsed + (ambientSince ? GetTickCount64() - ambientSince : 0); }
    int AmbientValue() const { return ambientPreview >= 0 ? ambientPreview : library.prefs.ambient; }
    void PreviewAmbient(float x);
    void SaveAmbient(int value);
    void ShowSearch(bool expand);
    void UpdateDrag();
    void EndDrag();
    void Scroll(float offset);
    void ScheduleIcons();
    void LoadIcons();
    UINT IconPixels() const;
    UINT CoverPixels() const;
    void StoreIcon(IconResult result);
    std::wstring IconKey(const Shortcut& entry, bool standard = false) const;
    IconCache* Icon(const Shortcut& entry, bool standard = false);
    IconCache* CachedIcon(const std::wstring& key);
    std::wstring CoverKey(const Shortcut& entry) const;
    void RequestIcon(const Shortcut& entry, bool standard = false);
    void RequestTileAssets(const Shortcut& entry);
    void Command(int id);
    void AddFiles(std::vector<fs::path> paths);
    void Context(int id, POINT position);
    std::span<const MenuItem> MenuItems() const;
    bool MenuEnabled(int action) const;
    bool MenuKey(UINT key);
    void CloseMenu();
    void RunMenu(int action);
    void OpenEditor(const std::wstring& id);
    void OpenCover();
    void FinishCover(bool keep);
    void SetCoverImage(const fs::path& path);
    bool CoverRemovable() const;
    bool HasCover() const;
    void PanCover(float dx, float dy);
    void ZoomCover(float amount);
    void CoverCommand(int id);
    void OpenDialog(Dialog next, const std::wstring& text = {});
    void CloseDialog();
    void Error(const std::wstring& text);
    void Launch(const std::wstring& id, bool elevated = false);
    void SortByUse();
    void RevealTarget(const std::wstring& id);
    bool UpdateHotkey();
    void ToggleFromHotkey();
    void Replace(const std::wstring& id);
    void Record(Preferences preferences);
    int BehaviorSelection(int id) const;
    void ToggleBehavior(int id);
    void Click(int id);
    void UpdateFonts();
    void ShowInitialWindow(int command);
    void CaptureWindowSize();
    void SaveWindowSize();
    bool AddTray();
    bool SendToTray();
    void Suspend(bool suspended);
    void RemoveTray();
    void Restore();
    void TrayMenu(POINT position);
    float ToDip(int pixel) const { return pixel * 96.f / dpi; }
    int ToPixel(float dip) const;
    float ContentY(int pixel) const { return ToDip(pixel) - TitleHeight; }
    int ClientY(float dip) const { return ToPixel(dip + TitleHeight); }
};

inline constexpr UiText AfterLaunchLabels[] = {Texts::Exit, Texts::ToTray, Texts::KeepOpen};
inline constexpr UiText MinimizeLabels[] = {Texts::ToTray, Texts::ToTaskbar};
inline constexpr UiText CloseLabels[] = {Texts::Exit, Texts::ToTray};
inline constexpr UiText HotkeyLabels[] = {Texts::HotkeyOff, Texts::HotkeyCtrlAltP, Texts::HotkeyCtrlAltG, Texts::HotkeyCtrlAltF12};
static_assert(std::size(HotkeyLabels) == HotkeyOptionCount);

struct BehaviorControl {
    int id;
    float top;
    UiText title;
    std::span<const UiText> options;
    float Expansion() const { return 8.f + options.size() * SettingsLayout::ChipHeight; }
};

inline constexpr BehaviorControl BehaviorControls[] = {
    {AfterLaunchControl, SettingsLayout::AfterLaunch, Texts::AfterLaunch, AfterLaunchLabels},
    {MinimizeControl, SettingsLayout::Minimize, Texts::WhenMinimized, MinimizeLabels},
    {CloseControl, SettingsLayout::Close, Texts::WhenClosed, CloseLabels},
    {HotkeyControl, SettingsLayout::Hotkey, Texts::GlobalHotkey, HotkeyLabels}
};

inline const BehaviorControl* BehaviorFor(int id) {
    for (const auto& control : BehaviorControls) {
        if (control.id == id) return &control;
    }
    return nullptr;
}
