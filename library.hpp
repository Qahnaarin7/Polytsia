#pragma once
#include <windows.h>
#include <filesystem>
#include <iterator>
#include <string>
#include <vector>
#include <winsqlite/winsqlite3.h>

namespace fs = std::filesystem;

struct Crop {
    float x = .5f, y = .5f, zoom = 1.f;
};

struct CoverChange {
    fs::path image;
    Crop crop;
    bool remove = false;
};

struct Shortcut {
    std::wstring id, name, source, file, icon, cover;
    Crop crop;
    int launches = 0;
};


enum class AfterLaunch { Exit, Tray, Keep };
enum class OnMinimize { Tray, Taskbar };
enum class OnClose { Exit, Tray };
enum class TileStyle { Centered, Cover, Compact };
enum class TileLabels { Always, Hover, Hidden };
enum class TileSize { Small, Medium, Large };
enum class TileSpacing { Tight, Normal, Wide };
inline constexpr int PaletteCount = 10;
inline constexpr int AmbientMax = 150;
inline constexpr float CropZoomMax = 3.f;

struct Preferences {
    int theme = 0; // System, light, dark
    bool motion = true;
    AfterLaunch afterLaunch = AfterLaunch::Exit;
    OnMinimize onMinimize = OnMinimize::Tray;
    OnClose onClose = OnClose::Exit;
    int lightPalette = 0;
    int darkPalette = 0;
    TileStyle tileStyle = TileStyle::Centered;
    int ambient = 100;
    TileLabels labels = TileLabels::Always;
    TileSize tileSize = TileSize::Medium;
    TileSpacing tileSpacing = TileSpacing::Normal;
    bool tileHalo = true;
    bool pauseMotionOnBattery = false;
    int globalHotkey = 0; // Index into ShowWindowHotkeys; 0 = off.
};

struct WindowSize {
    int width = 720, height = 520;
    bool maximized = false;
    bool operator==(const WindowSize&) const = default;
};

inline constexpr int SchemaVersion = 2;
enum class OpenResult { Ok, Failed, NewerVersion };

class Library {
    sqlite3* db_ = nullptr;
    bool Read();
    bool SavePositions(const std::vector<Shortcut>& entries, size_t first, size_t last);
public:
    fs::path root;
    std::vector<Shortcut> items;
    Preferences prefs;
    WindowSize windowSize;
    ~Library();
    OpenResult Open(const fs::path& directory);
    bool SetPreferences(Preferences preferences);
    bool SetWindowSize(WindowSize size);
    bool Save(const std::vector<Shortcut>& entries, Preferences preferences);
    fs::path File(const std::wstring& file) const { return root / L"items" / file; }
    fs::path StandardIcon(const Shortcut& entry) const { return File(entry.file + L".png"); }
    bool Backup(const fs::path& destination) const;
    OpenResult Restore(const fs::path& source);
    bool Add(const std::vector<fs::path>& paths, std::wstring& report);
    bool Replace(const std::wstring& id, const fs::path& source);
    bool Edit(const std::wstring& id, const std::wstring& name, const fs::path& icon, bool reset,
              const CoverChange* cover = nullptr, const struct Pixels* iconPreview = nullptr);
    bool Remove(const std::wstring& id);
    bool Move(const std::wstring& id, const std::wstring& target);
    bool RecordLaunch(const std::wstring& id);
    enum class SortResult { Sorted, Unchanged, NoLaunches, Failed };
    SortResult SortByLaunches();
    size_t Find(const std::wstring& id) const;
};

std::wstring NewId();
std::wstring Trim(std::wstring value);
std::wstring Fold(std::wstring value);
enum class FileKind { Shortcut, Icon, Cover, Backup };
std::vector<fs::path> ChooseFiles(HWND owner, FileKind kind, bool multi);
fs::path ChooseBackupDestination(HWND owner);
enum class LaunchResult { Opened, Failed, Cancelled };
LaunchResult LaunchShortcut(const fs::path& path, HWND owner, bool elevated = false);
bool ShortcutElevatable(const fs::path& path);
fs::path ShortcutTarget(const fs::path& path);
bool RevealInExplorer(const fs::path& path);
bool AutoStartRegistered();
bool SetAutoStart(bool enabled);
void RefreshAutoStart();

struct HotkeyOption {
    UINT modifiers, key;
};
inline constexpr HotkeyOption ShowWindowHotkeys[] = {
    {0, 0}, {MOD_CONTROL | MOD_ALT, 'P'}, {MOD_CONTROL | MOD_ALT, 'G'}, {MOD_CONTROL | MOD_ALT, VK_F12}
};
inline constexpr int HotkeyOptionCount = static_cast<int>(std::size(ShowWindowHotkeys));
