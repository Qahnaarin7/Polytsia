#include "graphics.hpp"
#include "localization.hpp"
#include "log.hpp"
#include <intshcut.h>
#include <propvarutil.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <shellapi.h>
#include <wrl/client.h>
#include <algorithm>
#include <cmath>
#include <cwctype>
#include <fstream>
#include <set>
#include <memory>

using Microsoft::WRL::ComPtr;

namespace {
constexpr wchar_t RunKey[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
constexpr wchar_t RunValue[] = L"Polytsia";
using Statement = std::unique_ptr<sqlite3_stmt, decltype(&sqlite3_finalize)>;

void LogSql(sqlite3* db, const char* sql) {
    const auto* message = static_cast<const wchar_t*>(sqlite3_errmsg16(db));
    Log(L"SQLite error %d: %s [%.60S]", sqlite3_extended_errcode(db), message ? message : L"", sql);
}

Statement Query(sqlite3* db, const char* sql, bool quiet = false) {
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK && !quiet) LogSql(db, sql);
    return {stmt, sqlite3_finalize};
}

bool Bind(sqlite3_stmt* stmt, int index, const std::wstring& value) {
    return sqlite3_bind_text16(stmt, index, value.c_str(), -1, SQLITE_TRANSIENT) == SQLITE_OK;
}

std::wstring Column(sqlite3_stmt* stmt, int column) {
    const auto* value = static_cast<const wchar_t*>(sqlite3_column_text16(stmt, column));
    return value ? value : L"";
}

bool Sql(sqlite3* db, const char* sql) {
    if (sqlite3_exec(db, sql, nullptr, nullptr, nullptr) == SQLITE_OK) return true;
    LogSql(db, sql);
    return false;
}

int UserVersion(sqlite3* db) {
    const auto query = Query(db, "PRAGMA user_version");
    return query && sqlite3_step(query.get()) == SQLITE_ROW ? sqlite3_column_int(query.get(), 0) : -1;
}

// Creates missing tables and upgrades the schema to SchemaVersion. Used for the live library and,
// on an in-memory copy, for backups made by older versions.
OpenResult PrepareSchema(sqlite3* db) {
    if (!Sql(db,
        "CREATE TABLE IF NOT EXISTS shortcuts(id TEXT PRIMARY KEY, name TEXT NOT NULL, source TEXT NOT NULL,"
        " file TEXT NOT NULL, icon TEXT NOT NULL, position INTEGER NOT NULL);"
        "CREATE TABLE IF NOT EXISTS preferences(singleton INTEGER PRIMARY KEY, theme INTEGER NOT NULL,"
        " close_after INTEGER NOT NULL, motion INTEGER NOT NULL);"
        "CREATE TABLE IF NOT EXISTS appearance(singleton INTEGER PRIMARY KEY, light_palette INTEGER NOT NULL,"
        " dark_palette INTEGER NOT NULL, tile_style INTEGER NOT NULL);"
        "CREATE TABLE IF NOT EXISTS visual_effects(singleton INTEGER PRIMARY KEY, ambient INTEGER NOT NULL);"
        "CREATE TABLE IF NOT EXISTS tile_labels(singleton INTEGER PRIMARY KEY, mode INTEGER NOT NULL);"
        "CREATE TABLE IF NOT EXISTS interface_options(singleton INTEGER PRIMARY KEY, tile_size INTEGER NOT NULL,"
        " tile_spacing INTEGER NOT NULL, tile_halo INTEGER NOT NULL, pause_on_battery INTEGER NOT NULL);"
        "CREATE TABLE IF NOT EXISTS shortcut_covers(id TEXT PRIMARY KEY, file TEXT NOT NULL,"
        " x REAL NOT NULL, y REAL NOT NULL, zoom REAL NOT NULL);"
        "CREATE TABLE IF NOT EXISTS window_state(singleton INTEGER PRIMARY KEY, width INTEGER NOT NULL,"
        " height INTEGER NOT NULL, maximized INTEGER NOT NULL);")) return OpenResult::Failed;
    const int version = UserVersion(db);
    if (version < 0) return OpenResult::Failed;
    if (version > SchemaVersion) {
        Log(L"Library schema %d is newer than supported %d", version, SchemaVersion);
        return OpenResult::NewerVersion;
    }
    static constexpr const char* Migrations[SchemaVersion] = {
        // 0 -> 1: window behavior options.
        "ALTER TABLE preferences ADD COLUMN after_launch INTEGER NOT NULL DEFAULT 0;"
        "ALTER TABLE preferences ADD COLUMN on_minimize INTEGER NOT NULL DEFAULT 0;"
        "ALTER TABLE preferences ADD COLUMN on_close INTEGER NOT NULL DEFAULT 0;"
        "UPDATE preferences SET after_launch=2 WHERE close_after=0;",
        // 1 -> 2: launch counters and the global hotkey option.
        "ALTER TABLE shortcuts ADD COLUMN launches INTEGER NOT NULL DEFAULT 0;"
        "ALTER TABLE interface_options ADD COLUMN global_hotkey INTEGER NOT NULL DEFAULT 0;",
    };
    for (int step = version; step < SchemaVersion; ++step) {
        const std::string sql = std::string("BEGIN;") + Migrations[step] +
            "PRAGMA user_version=" + std::to_string(step + 1) + "; COMMIT;";
        if (!Sql(db, sql.c_str())) {
            sqlite3_exec(db, "ROLLBACK", nullptr, nullptr, nullptr);
            Log(L"Schema migration %d -> %d failed", step, step + 1);
            return OpenResult::Failed;
        }
    }
    return OpenResult::Ok;
}

bool WritePreferences(sqlite3* db, const Preferences& preferences) {
    const auto options = Query(db,
        "INSERT OR REPLACE INTO preferences(singleton,theme,close_after,motion,after_launch,on_minimize,on_close)"
        " VALUES(1,?,?,?,?,?,?)");
    const auto appearance = Query(db, "INSERT OR REPLACE INTO appearance VALUES(1,?,?,?)");
    const auto effects = Query(db, "INSERT OR REPLACE INTO visual_effects VALUES(1,?)");
    const auto labels = Query(db, "INSERT OR REPLACE INTO tile_labels VALUES(1,?)");
    const auto interfaceOptions = Query(db, "INSERT OR REPLACE INTO interface_options"
        "(singleton,tile_size,tile_spacing,tile_halo,pause_on_battery,global_hotkey) VALUES(1,?,?,?,?,?)");
    return options && appearance && effects && labels && interfaceOptions &&
        sqlite3_bind_int(options.get(), 1, preferences.theme) == SQLITE_OK &&
        sqlite3_bind_int(options.get(), 2, preferences.afterLaunch == AfterLaunch::Exit) == SQLITE_OK &&
        sqlite3_bind_int(options.get(), 3, preferences.motion) == SQLITE_OK &&
        sqlite3_bind_int(options.get(), 4, static_cast<int>(preferences.afterLaunch)) == SQLITE_OK &&
        sqlite3_bind_int(options.get(), 5, static_cast<int>(preferences.onMinimize)) == SQLITE_OK &&
        sqlite3_bind_int(options.get(), 6, static_cast<int>(preferences.onClose)) == SQLITE_OK &&
        sqlite3_step(options.get()) == SQLITE_DONE &&
        sqlite3_bind_int(appearance.get(), 1, preferences.lightPalette) == SQLITE_OK &&
        sqlite3_bind_int(appearance.get(), 2, preferences.darkPalette) == SQLITE_OK &&
        sqlite3_bind_int(appearance.get(), 3, static_cast<int>(preferences.tileStyle)) == SQLITE_OK &&
        sqlite3_step(appearance.get()) == SQLITE_DONE &&
        sqlite3_bind_int(effects.get(), 1, preferences.ambient) == SQLITE_OK &&
        sqlite3_step(effects.get()) == SQLITE_DONE &&
        sqlite3_bind_int(labels.get(), 1, static_cast<int>(preferences.labels)) == SQLITE_OK &&
        sqlite3_step(labels.get()) == SQLITE_DONE &&
        sqlite3_bind_int(interfaceOptions.get(), 1, static_cast<int>(preferences.tileSize)) == SQLITE_OK &&
        sqlite3_bind_int(interfaceOptions.get(), 2, static_cast<int>(preferences.tileSpacing)) == SQLITE_OK &&
        sqlite3_bind_int(interfaceOptions.get(), 3, preferences.tileHalo) == SQLITE_OK &&
        sqlite3_bind_int(interfaceOptions.get(), 4, preferences.pauseMotionOnBattery) == SQLITE_OK &&
        sqlite3_bind_int(interfaceOptions.get(), 5, preferences.globalHotkey) == SQLITE_OK &&
        sqlite3_step(interfaceOptions.get()) == SQLITE_DONE;
}

bool Leaf(const std::wstring& value) {
    return !value.empty() && value != L"." && value != L".." &&
        value.find_first_of(L"/\\:") == std::wstring::npos;
}

void DeleteCopy(const fs::path& path) {
    if (!DeleteFileW(path.c_str()) && GetLastError() == ERROR_ACCESS_DENIED) {
        const DWORD attrs = GetFileAttributesW(path.c_str());
        if (attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_READONLY)) {
            SetFileAttributesW(path.c_str(), attrs & ~FILE_ATTRIBUTE_READONLY);
            DeleteFileW(path.c_str());
        }
    }
}

bool CopyShortcut(const fs::path& source, const fs::path& destination) {
    const auto extension = Fold(source.extension().wstring());
    if (extension != L".lnk" && extension != L".url") return false;
    ComPtr<IShellLinkW> link;
    ComPtr<IPersistFile> persist;
    bool rebase = false;
    if (extension == L".lnk") {
        if (FAILED(CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&link))) ||
            FAILED(link.As(&persist)) || FAILED(persist->Load(source.c_str(), STGM_READ))) return false;
        ComPtr<IShellLinkDataList> data;
        DWORD flags = 0;
        if (SUCCEEDED(link.As(&data)) && SUCCEEDED(data->GetFlags(&flags))) {
            rebase = (flags & SLDF_HAS_RELPATH) != 0;
        }
        if (rebase) {
            // Resolve with the original file as the base. Preserve the shell item ID,
            // arguments, working directory and flags rather than constructing a new link.
            link->SetRelativePath(source.c_str(), 0);
            if (FAILED(link->Resolve(nullptr, SLR_NO_UI | SLR_NOSEARCH | SLR_NOTRACK | (400u << 16)))) {
                wchar_t absolute[32768]{};
                if (FAILED(link->GetPath(absolute, ARRAYSIZE(absolute), nullptr, SLGP_RAWPATH)) ||
                    (!fs::path(absolute).is_absolute() && absolute[0] != L'%')) return false;
            }
        }
    }

    if (!CopyFileW(source.c_str(), destination.c_str(), TRUE)) return false;
    if (!rebase) return true;

    const DWORD attrs = GetFileAttributesW(destination.c_str());
    if (attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_READONLY)) {
        SetFileAttributesW(destination.c_str(), attrs & ~FILE_ATTRIBUTE_READONLY);
    }
    if (FAILED(link->SetRelativePath(destination.c_str(), 0)) ||
        FAILED(persist->Save(destination.c_str(), TRUE))) {
        DeleteCopy(destination);
        return false;
    }
    const std::wstring zone = L":Zone.Identifier";
    CopyFileW((source.wstring() + zone).c_str(), (destination.wstring() + zone).c_str(), TRUE);
    return true;
}

std::wstring ManagedShortcutExtension(const fs::path& source) {
    const auto extension = Fold(source.extension().wstring());
    if (extension == L".lnk" || extension == L".exe") return L".lnk";
    return extension == L".url" ? extension : std::wstring{};
}

bool SaveShortcut(const fs::path& source, const fs::path& destination) {
    if (Fold(source.extension().wstring()) != L".exe") return CopyShortcut(source, destination);
    DWORD binaryType = 0;
    if (!GetBinaryTypeW(source.c_str(), &binaryType)) return false;
    ComPtr<IShellLinkW> link;
    ComPtr<IPersistFile> persist;
    if (FAILED(CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&link))) ||
        FAILED(link.As(&persist)) || FAILED(link->SetPath(source.c_str())) ||
        FAILED(link->SetWorkingDirectory(source.parent_path().c_str()))) return false;
    if (FAILED(persist->Save(destination.c_str(), TRUE))) {
        DeleteCopy(destination);
        return false;
    }
    return true;
}

Crop SafeCrop(Crop crop) {
    auto inRange = [](float value, float fallback, float minimum, float maximum) {
        return std::isfinite(value) ? std::clamp(value, minimum, maximum) : fallback;
    };
    return {inRange(crop.x, .5f, 0.f, 1.f), inRange(crop.y, .5f, 0.f, 1.f),
            inRange(crop.zoom, 1.f, 1.f, CropZoomMax)};
}
}

std::wstring NewId() {
    GUID guid{};
    if (FAILED(CoCreateGuid(&guid))) return {};
    wchar_t result[40]{};
    StringFromGUID2(guid, result, ARRAYSIZE(result));
    return std::wstring(result + 1, 36);
}

std::wstring Trim(std::wstring value) {
    const auto begin = std::find_if_not(value.begin(), value.end(), [](wchar_t ch) { return iswspace(ch); });
    const auto end = std::find_if_not(value.rbegin(), value.rend(), [](wchar_t ch) { return iswspace(ch); }).base();
    return begin < end ? std::wstring(begin, end) : std::wstring{};
}

std::wstring Fold(std::wstring value) {
    if (!value.empty()) CharLowerBuffW(value.data(), static_cast<DWORD>(value.size()));
    return value;
}

Library::~Library() {
    if (db_) sqlite3_close(db_);
}

OpenResult Library::Open(const fs::path& directory) {
    root = directory;
    std::error_code error;
    fs::create_directories(root / L"items", error);
    if (error) {
        Log(L"Couldn't create the data folder: %S", error.message().c_str());
        return OpenResult::Failed;
    }
    if (sqlite3_open16((root / L"library.sqlite").c_str(), &db_) != SQLITE_OK) {
        Log(L"Couldn't open library.sqlite");
        return OpenResult::Failed;
    }
    const auto prepared = PrepareSchema(db_);
    if (prepared != OpenResult::Ok) return prepared;
    if (!Read()) {
        Log(L"Couldn't read the library");
        return OpenResult::Failed;
    }
    return OpenResult::Ok;
}

bool Library::Read() {
    const auto rows = Query(db_, "SELECT shortcuts.id,name,source,shortcuts.file,icon,shortcut_covers.file,x,y,zoom,launches "
                                  "FROM shortcuts LEFT JOIN shortcut_covers ON shortcuts.id=shortcut_covers.id ORDER BY position");
    const auto settings = Query(db_, "SELECT theme,motion,after_launch,on_minimize,on_close FROM preferences WHERE singleton=1");
    const auto appearance = Query(db_, "SELECT light_palette,dark_palette,tile_style FROM appearance WHERE singleton=1");
    const auto effects = Query(db_, "SELECT ambient FROM visual_effects WHERE singleton=1");
    const auto labels = Query(db_, "SELECT mode FROM tile_labels WHERE singleton=1");
    const auto placement = Query(db_, "SELECT width,height,maximized FROM window_state WHERE singleton=1");
    if (!rows || !settings || !appearance || !effects || !labels || !placement) return false;
    int result;
    while ((result = sqlite3_step(rows.get())) == SQLITE_ROW) {
        Shortcut row{Column(rows.get(), 0), Column(rows.get(), 1), Column(rows.get(), 2),
                     Column(rows.get(), 3), Column(rows.get(), 4), Column(rows.get(), 5), {}};
        if (!Leaf(row.file) || (!row.icon.empty() && !Leaf(row.icon))) return false;
        if (!row.cover.empty() && Leaf(row.cover)) {
            row.crop = SafeCrop({static_cast<float>(sqlite3_column_double(rows.get(), 6)),
                                 static_cast<float>(sqlite3_column_double(rows.get(), 7)),
                                 static_cast<float>(sqlite3_column_double(rows.get(), 8))});
        } else row.cover.clear();
        row.launches = std::max(0, sqlite3_column_int(rows.get(), 9));
        items.push_back(std::move(row));
    }
    if (result != SQLITE_DONE) return false;
    result = sqlite3_step(settings.get());
    if (result == SQLITE_ROW) {
        prefs = {std::clamp(sqlite3_column_int(settings.get(), 0), 0, 2),
                 sqlite3_column_int(settings.get(), 1) != 0,
                 static_cast<AfterLaunch>(std::clamp(sqlite3_column_int(settings.get(), 2), 0, 2)),
                 static_cast<OnMinimize>(std::clamp(sqlite3_column_int(settings.get(), 3), 0, 1)),
                 static_cast<OnClose>(std::clamp(sqlite3_column_int(settings.get(), 4), 0, 1))};
    }
    if (result != SQLITE_ROW && result != SQLITE_DONE) return false;
    result = sqlite3_step(appearance.get());
    if (result == SQLITE_ROW) {
        prefs.lightPalette = std::clamp(sqlite3_column_int(appearance.get(), 0), 0, PaletteCount - 1);
        prefs.darkPalette = std::clamp(sqlite3_column_int(appearance.get(), 1), 0, PaletteCount - 1);
        prefs.tileStyle = static_cast<TileStyle>(std::clamp(sqlite3_column_int(appearance.get(), 2),
                                                           0, static_cast<int>(TileStyle::Compact)));
    }
    if (result != SQLITE_ROW && result != SQLITE_DONE) return false;
    result = sqlite3_step(effects.get());
    if (result == SQLITE_ROW) prefs.ambient = std::clamp(sqlite3_column_int(effects.get(), 0), 0, AmbientMax);
    if (result != SQLITE_ROW && result != SQLITE_DONE) return false;
    result = sqlite3_step(labels.get());
    if (result == SQLITE_ROW) prefs.labels = static_cast<TileLabels>(std::clamp(sqlite3_column_int(labels.get(), 0), 0, 2));
    if (result != SQLITE_ROW && result != SQLITE_DONE) return false;
    // Older backup files do not contain the interface_options table.
    if (auto options = Query(db_, "SELECT tile_size,tile_spacing,tile_halo,pause_on_battery,global_hotkey"
                                  " FROM interface_options WHERE singleton=1")) {
        result = sqlite3_step(options.get());
        if (result == SQLITE_ROW) {
            prefs.tileSize = static_cast<TileSize>(std::clamp(sqlite3_column_int(options.get(), 0), 0, 2));
            prefs.tileSpacing = static_cast<TileSpacing>(std::clamp(sqlite3_column_int(options.get(), 1), 0, 2));
            prefs.tileHalo = sqlite3_column_int(options.get(), 2) != 0;
            prefs.pauseMotionOnBattery = sqlite3_column_int(options.get(), 3) != 0;
            prefs.globalHotkey = sqlite3_column_int(options.get(), 4) != 0;
        }
        if (result != SQLITE_ROW && result != SQLITE_DONE) return false;
    }
    result = sqlite3_step(placement.get());
    if (result == SQLITE_ROW) {
        const int width = sqlite3_column_int(placement.get(), 0);
        const int height = sqlite3_column_int(placement.get(), 1);
        if (width > 0 && width <= 32767 && height > 0 && height <= 32767) {
            windowSize = {width, height, sqlite3_column_int(placement.get(), 2) != 0};
        }
    }
    return result == SQLITE_ROW || result == SQLITE_DONE;
}

bool Library::SetWindowSize(WindowSize size) {
    if (size == windowSize) return true;
    const auto placement = Query(db_, "INSERT OR REPLACE INTO window_state VALUES(1,?,?,?)");
    if (!placement || sqlite3_bind_int(placement.get(), 1, size.width) != SQLITE_OK ||
        sqlite3_bind_int(placement.get(), 2, size.height) != SQLITE_OK ||
        sqlite3_bind_int(placement.get(), 3, size.maximized) != SQLITE_OK ||
        sqlite3_step(placement.get()) != SQLITE_DONE) return false;
    windowSize = size;
    return true;
}

bool Library::SetPreferences(Preferences preferences) {
    if (!Sql(db_, "BEGIN IMMEDIATE")) return false;
    if (!WritePreferences(db_, preferences) || !Sql(db_, "COMMIT")) {
        Sql(db_, "ROLLBACK");
        return false;
    }
    prefs = preferences;
    return true;
}

bool Library::Save(const std::vector<Shortcut>& entries, Preferences preferences) {
    if (!Sql(db_, "BEGIN IMMEDIATE")) return false;
    const auto item = Query(db_, "INSERT INTO shortcuts(id,name,source,file,icon,position,launches) VALUES(?,?,?,?,?,?,?)");
    const auto cover = Query(db_, "INSERT INTO shortcut_covers VALUES(?,?,?,?,?)");
    bool ok = item && cover && Sql(db_, "DELETE FROM shortcuts; DELETE FROM shortcut_covers;");
    for (size_t i = 0; ok && i < entries.size(); ++i) {
        const auto& entry = entries[i];
        ok = Bind(item.get(), 1, entry.id) && Bind(item.get(), 2, entry.name) &&
            Bind(item.get(), 3, entry.source) && Bind(item.get(), 4, entry.file) &&
            Bind(item.get(), 5, entry.icon) &&
            sqlite3_bind_int64(item.get(), 6, static_cast<sqlite3_int64>(i)) == SQLITE_OK &&
            sqlite3_bind_int(item.get(), 7, entry.launches) == SQLITE_OK &&
            sqlite3_step(item.get()) == SQLITE_DONE;
        sqlite3_reset(item.get());
        if (ok && !entry.cover.empty()) {
            ok = Bind(cover.get(), 1, entry.id) && Bind(cover.get(), 2, entry.cover) &&
                sqlite3_bind_double(cover.get(), 3, entry.crop.x) == SQLITE_OK &&
                sqlite3_bind_double(cover.get(), 4, entry.crop.y) == SQLITE_OK &&
                sqlite3_bind_double(cover.get(), 5, entry.crop.zoom) == SQLITE_OK &&
                sqlite3_step(cover.get()) == SQLITE_DONE;
            sqlite3_reset(cover.get());
        }
    }
    ok = ok && WritePreferences(db_, preferences) && Sql(db_, "COMMIT");
    if (!ok) {
        Log(L"Saving the library failed");
        Sql(db_, "ROLLBACK");
        return false;
    }
    items = entries;
    prefs = preferences;
    return true;
}

size_t Library::Find(const std::wstring& id) const {
    const auto match = std::find_if(items.begin(), items.end(), [&](const Shortcut& item) { return item.id == id; });
    return static_cast<size_t>(match - items.begin());
}

bool Library::Add(const std::vector<fs::path>& paths, std::wstring& report) {
    auto updated = items;
    std::vector<fs::path> created;
    size_t duplicates = 0, failures = 0;
    for (const auto& path : paths) {
        std::error_code error;
        const auto absolute = fs::absolute(path, error).lexically_normal();
        const auto extension = ManagedShortcutExtension(absolute);
        if (error || extension.empty()) { ++failures; continue; }
        const auto& source = absolute.native();
        if (std::any_of(updated.begin(), updated.end(), [&](const Shortcut& item) {
            return CompareStringOrdinal(item.source.c_str(), -1, source.c_str(), -1, TRUE) == CSTR_EQUAL;
        })) {
            ++duplicates;
            continue;
        }
        const auto id = NewId();
        const auto file = id + extension;
        const auto destination = File(file);
        if (id.empty() || !SaveShortcut(absolute, destination)) {
            ++failures;
            continue;
        }
        updated.push_back({id, absolute.stem().wstring(), source, file, {}, {}, {}});
        created.push_back(destination);
    }
    if (!created.empty() && !Save(updated, prefs)) {
        for (const auto& file : created) DeleteCopy(file);
        report = Texts::SaveLibraryError.Get();
        return false;
    }
    const auto counts = Texts::AddedCount.Get() + std::to_wstring(created.size());
    if (failures) report = counts + Texts::FailedCount.Get() + std::to_wstring(failures) + Texts::ShortcutFormatsHint.Get();
    if (duplicates) report += (report.empty() ? counts : L"") + Texts::DuplicateCount.Get() + std::to_wstring(duplicates) + L".";
    return !created.empty();
}

bool Library::Replace(const std::wstring& id, const fs::path& source) {
    const size_t index = Find(id);
    if (index == items.size()) return false;
    const auto newId = NewId();
    std::error_code error;
    const auto absolute = fs::absolute(source, error).lexically_normal();
    const auto extension = ManagedShortcutExtension(absolute);
    if (newId.empty() || error || extension.empty()) return false;
    const auto file = newId + extension;
    if (!SaveShortcut(absolute, File(file))) return false;
    const auto previous = items[index];
    auto updated = items;
    updated[index].file = file;
    updated[index].source = absolute.wstring();
    if (!Save(updated, prefs)) {
        DeleteCopy(File(file));
        return false;
    }
    DeleteCopy(File(previous.file));
    DeleteCopy(StandardIcon(previous));
    return true;
}

bool Library::Edit(const std::wstring& id, const std::wstring& name, const fs::path& icon, bool reset,
                   const CoverChange* cover, const Pixels* iconPreview) {
    const size_t index = Find(id);
    if (index == items.size() || name.empty()) return false;
    const auto previous = items[index];
    auto updated = items;
    auto& entry = updated[index];
    entry.name = name;
    std::vector<fs::path> created;
    auto stage = [&](const fs::path& source, std::wstring& filename) {
        const auto newId = NewId();
        if (newId.empty()) return false;
        filename = newId + Fold(source.extension().wstring());
        const auto destination = File(filename);
        if (!CopyFileW(source.c_str(), destination.c_str(), TRUE)) return false;
        created.push_back(destination);
        return true;
    };
    if (reset) entry.icon.clear();
    if (Fold(icon.extension().wstring()) == L".exe") {
        const auto newId = NewId();
        if (newId.empty() || !iconPreview) return false;
        entry.icon = newId + L".png";
        const auto destination = File(entry.icon);
        if (!WriteIconPng(*iconPreview, destination)) {
            DeleteCopy(destination);
            return false;
        }
        created.push_back(destination);
    } else if (!icon.empty() && !stage(icon, entry.icon)) return false;
    bool valid = true;
    if (cover) {
        if (cover->remove) {
            entry.cover.clear();
            entry.crop = {};
        } else {
            if (!cover->image.empty()) {
                const auto extension = Fold(cover->image.extension().wstring());
                valid = (extension == L".png" || extension == L".jpg" || extension == L".jpeg") &&
                    stage(cover->image, entry.cover);
            }
            entry.crop = entry.cover.empty() ? Crop{} : SafeCrop(cover->crop);
        }
    }
    if (!valid || !Save(updated, prefs)) {
        for (const auto& file : created) DeleteCopy(file);
        return false;
    }
    if (!previous.icon.empty() && previous.icon != entry.icon) DeleteCopy(File(previous.icon));
    if (!previous.cover.empty() && previous.cover != entry.cover) DeleteCopy(File(previous.cover));
    return true;
}

bool Library::Remove(const std::wstring& id) {
    const size_t index = Find(id);
    if (index == items.size()) return false;
    auto updated = items;
    const auto removed = updated[index];
    updated.erase(updated.begin() + static_cast<ptrdiff_t>(index));
    if (!Save(updated, prefs)) return false;
    DeleteCopy(File(removed.file));
    DeleteCopy(StandardIcon(removed));
    if (!removed.icon.empty()) DeleteCopy(File(removed.icon));
    if (!removed.cover.empty()) DeleteCopy(File(removed.cover));
    return true;
}

bool Library::SavePositions(const std::vector<Shortcut>& entries, size_t first, size_t last) {
    if (!Sql(db_, "BEGIN IMMEDIATE")) return false;
    const auto update = Query(db_, "UPDATE shortcuts SET position=? WHERE id=?");
    bool ok = static_cast<bool>(update);
    for (size_t i = first; ok && i <= last && i < entries.size(); ++i) {
        ok = sqlite3_bind_int64(update.get(), 1, static_cast<sqlite3_int64>(i)) == SQLITE_OK &&
            Bind(update.get(), 2, entries[i].id) && sqlite3_step(update.get()) == SQLITE_DONE;
        sqlite3_reset(update.get());
    }
    if (!ok || !Sql(db_, "COMMIT")) {
        Sql(db_, "ROLLBACK");
        return false;
    }
    items = entries;
    return true;
}

bool Library::Move(const std::wstring& id, const std::wstring& target) {
    const size_t origin = Find(id), destination = Find(target);
    if (origin == items.size() || destination == items.size() || origin == destination) return true;
    auto updated = items;
    if (origin < destination) {
        std::rotate(updated.begin() + origin, updated.begin() + origin + 1, updated.begin() + destination + 1);
    } else {
        std::rotate(updated.begin() + destination, updated.begin() + origin, updated.begin() + origin + 1);
    }
    return SavePositions(updated, std::min(origin, destination), std::max(origin, destination));
}

bool Library::RecordLaunch(const std::wstring& id) {
    const size_t index = Find(id);
    if (index == items.size()) return false;
    const auto update = Query(db_, "UPDATE shortcuts SET launches=launches+1 WHERE id=?");
    if (!update || !Bind(update.get(), 1, id) || sqlite3_step(update.get()) != SQLITE_DONE) return false;
    ++items[index].launches;
    return true;
}

bool Library::SortByLaunches() {
    if (items.empty()) return true;
    auto updated = items;
    std::stable_sort(updated.begin(), updated.end(), [](const Shortcut& a, const Shortcut& b) {
        return a.launches > b.launches;
    });
    return SavePositions(updated, 0, updated.size() - 1);
}

namespace {
constexpr int BackupApplicationId = 0x504F4C59;
constexpr size_t AssetLimit = 32 * 1024 * 1024, ZoneLimit = 65536;
constexpr auto ZoneStream = L":Zone.Identifier";

bool ReadAsset(const fs::path& path, size_t limit, std::vector<char>& bytes) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) return false;
    const auto size = file.tellg();
    if (size <= 0 || static_cast<uint64_t>(size) > limit) return false;
    bytes.resize(static_cast<size_t>(size));
    file.seekg(0);
    return static_cast<bool>(file.read(bytes.data(), size));
}

bool WriteAsset(const fs::path& path, const void* data, int size) {
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file) return false;
    file.write(static_cast<const char*>(data), size);
    file.close();
    return static_cast<bool>(file);
}

bool WriteBackupAsset(sqlite3_stmt* stmt, const std::wstring& name, const fs::path& file, const Pixels* pixels = nullptr) {
    std::vector<char> bytes, zone;
    if (!pixels && !ReadAsset(file, AssetLimit, bytes)) return false;
    if (!pixels) ReadAsset(fs::path(file.wstring() + ZoneStream), ZoneLimit, zone);
    const auto data = pixels ? static_cast<const void*>(pixels->bytes.data()) : bytes.data();
    const auto size = pixels ? pixels->bytes.size() : bytes.size();
    sqlite3_reset(stmt);
    return Bind(stmt, 1, name) &&
        sqlite3_bind_blob(stmt, 2, data, static_cast<int>(size), SQLITE_TRANSIENT) == SQLITE_OK &&
        sqlite3_bind_blob(stmt, 3, zone.data(), static_cast<int>(zone.size()), SQLITE_TRANSIENT) == SQLITE_OK &&
        sqlite3_bind_int(stmt, 4, pixels ? static_cast<int>(pixels->width) : 0) == SQLITE_OK &&
        sqlite3_bind_int(stmt, 5, pixels ? static_cast<int>(pixels->height) : 0) == SQLITE_OK &&
        sqlite3_step(stmt) == SQLITE_DONE && sqlite3_reset(stmt) == SQLITE_OK &&
        sqlite3_clear_bindings(stmt) == SQLITE_OK;
}

bool RestoreAsset(sqlite3_stmt* stmt, const std::wstring& name, const fs::path& path, bool optionalIcon) {
    sqlite3_reset(stmt);
    if (!Bind(stmt, 1, name)) return false;
    const int result = sqlite3_step(stmt);
    if (result == SQLITE_DONE) return optionalIcon;
    if (result != SQLITE_ROW || sqlite3_column_type(stmt, 0) != SQLITE_BLOB) return false;
    const int bytes = sqlite3_column_bytes(stmt, 0), zone = sqlite3_column_bytes(stmt, 1);
    const auto* data = static_cast<const BYTE*>(sqlite3_column_blob(stmt, 0));
    const int width = sqlite3_column_int(stmt, 2), height = sqlite3_column_int(stmt, 3);
    if (!data || bytes <= 0 || bytes > AssetLimit || zone > ZoneLimit) return false;
    if (width || height) {
        if (!optionalIcon || width <= 0 || height <= 0 || width > 256 || height > 256 ||
            bytes != width * height * 4 || zone) return false;
        Pixels pixels;
        pixels.width = static_cast<UINT>(width);
        pixels.height = static_cast<UINT>(height);
        pixels.bytes.assign(data, data + bytes);
        return WriteIconPng(pixels, path);
    }
    return WriteAsset(path, data, bytes) && (zone == 0 ||
        WriteAsset(fs::path(path.wstring() + ZoneStream), sqlite3_column_blob(stmt, 1), zone));
}
}

bool Library::Backup(const fs::path& destination) const {
    if (Fold(destination.extension().wstring()) != L".polytsia") return false;
    const auto token = NewId();
    if (token.empty()) return false;
    const auto temp = destination.parent_path() / (L"Polytsia-" + token + L".tmp");
    bool ok = false;
    {
        Library snapshot;
        if (sqlite3_open16(temp.c_str(), &snapshot.db_) == SQLITE_OK) {
            auto* copy = sqlite3_backup_init(snapshot.db_, "main", db_, "main");
            if (copy) {
                ok = sqlite3_backup_step(copy, -1) == SQLITE_DONE;
                ok = sqlite3_backup_finish(copy) == SQLITE_OK && ok;
            }
        }
        ok = ok && Sql(snapshot.db_, "BEGIN; PRAGMA application_id=1347374169;"
            "CREATE TABLE backup_assets(name TEXT PRIMARY KEY, data BLOB NOT NULL, zone BLOB, width INT, height INT);");
        const auto insert = ok ? Query(snapshot.db_, "INSERT INTO backup_assets VALUES(?,?,?,?,?)") : Statement{nullptr, sqlite3_finalize};
        ok = ok && static_cast<bool>(insert);
        std::set<std::wstring> exported;
        for (const auto& entry : items) {
            if (!ok) break;
            for (const auto* name : {&entry.file, &entry.icon, &entry.cover}) {
                if (!name->empty() && exported.insert(*name).second && !WriteBackupAsset(insert.get(), *name, File(*name))) {
                    ok = false;
                    break;
                }
            }
            if (!ok) break;
            const auto sidecar = StandardIcon(entry);
            const auto name = sidecar.filename().wstring();
            if (!exported.insert(name).second) continue;
            if (GetFileAttributesW(sidecar.c_str()) != INVALID_FILE_ATTRIBUTES) {
                ok = WriteBackupAsset(insert.get(), name, sidecar);
            } else if (const auto icon = ReadStandardIcon(File(entry.file), 256)) {
                ok = WriteBackupAsset(insert.get(), name, {}, icon.get());
            }
        }
        ok = ok && Sql(snapshot.db_, "COMMIT");
    }
    if (ok) ok = MoveFileExW(temp.c_str(), destination.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != FALSE;
    if (!ok) DeleteCopy(temp);
    return ok;
}

OpenResult Library::Restore(const fs::path& source) {
    // Copy the backup into memory, then validate and migrate the copy. The file itself is never written.
    Library snapshot;
    {
        sqlite3* file = nullptr;
        const auto filename = source.u8string();
        const bool opened = sqlite3_open_v2(reinterpret_cast<const char*>(filename.c_str()), &file,
                                            SQLITE_OPEN_READONLY, nullptr) == SQLITE_OK;
        bool ok = opened && Sql(file, "PRAGMA trusted_schema=OFF; PRAGMA query_only=ON;");
        if (ok) {
            const auto id = Query(file, "PRAGMA application_id");
            ok = id && sqlite3_step(id.get()) == SQLITE_ROW && sqlite3_column_int(id.get(), 0) == BackupApplicationId;
        }
        ok = ok && sqlite3_open16(L":memory:", &snapshot.db_) == SQLITE_OK;
        if (ok) {
            auto* copy = sqlite3_backup_init(snapshot.db_, "main", file, "main");
            ok = copy && sqlite3_backup_step(copy, -1) == SQLITE_DONE;
            if (copy) ok = sqlite3_backup_finish(copy) == SQLITE_OK && ok;
        }
        sqlite3_close(file);
        if (!ok) {
            Log(L"Backup file is not a readable Polytsia backup");
            return OpenResult::Failed;
        }
    }
    sqlite3_limit(snapshot.db_, SQLITE_LIMIT_LENGTH, static_cast<int>(AssetLimit + ZoneLimit + 4096));
    if (!Sql(snapshot.db_, "PRAGMA trusted_schema=OFF")) return OpenResult::Failed;
    {
        // A backup only ever contains tables. Refuse triggers and views before running migrations on it.
        const auto extras = Query(snapshot.db_, "SELECT count(*) FROM sqlite_master WHERE type IN ('trigger','view')");
        if (!extras || sqlite3_step(extras.get()) != SQLITE_ROW || sqlite3_column_int(extras.get(), 0) != 0) {
            Log(L"Backup contains unexpected schema objects");
            return OpenResult::Failed;
        }
    }
    const auto prepared = PrepareSchema(snapshot.db_);
    if (prepared != OpenResult::Ok) return prepared;
    if (!snapshot.Read() || !Sql(snapshot.db_, "PRAGMA query_only=ON; BEGIN;")) return OpenResult::Failed;
    const auto asset = Query(snapshot.db_, "SELECT data,zone,width,height FROM backup_assets WHERE name=?");
    if (!asset) return OpenResult::Failed;

    std::vector<fs::path> staged;
    auto extract = [&](const std::wstring& name, const fs::path& path, bool optionalIcon = false) {
        staged.push_back(path);
        return RestoreAsset(asset.get(), name, path, optionalIcon);
    };
    auto replaceImage = [&](std::wstring& name, bool cover) {
        if (name.empty()) return true;
        const auto extension = Fold(fs::path(name).extension().wstring());
        if (extension != L".png" && !(cover ? extension == L".jpg" || extension == L".jpeg" : extension == L".ico")) {
            return false;
        }
        const auto token = NewId();
        if (token.empty() || !extract(name, File(token + extension))) return false;
        name = token + extension;
        return true;
    };
    bool ok = true;
    for (auto& entry : snapshot.items) {
        const auto extension = Fold(fs::path(entry.file).extension().wstring());
        const auto token = NewId();
        if (token.empty() || (extension != L".lnk" && extension != L".url")) { ok = false; break; }
        const auto original = entry.file;
        entry.file = token + extension;
        if (!extract(original, File(entry.file)) || !extract(original + L".png", StandardIcon(entry), true) ||
            !replaceImage(entry.icon, false) || !replaceImage(entry.cover, true)) {
            ok = false;
            break;
        }
    }
    const auto previous = items;
    if (!ok || !Save(snapshot.items, snapshot.prefs)) {
        for (const auto& file : staged) DeleteCopy(file);
        Log(L"Restoring the backup failed");
        return OpenResult::Failed;
    }
    for (const auto& entry : previous) {
        DeleteCopy(File(entry.file));
        DeleteCopy(StandardIcon(entry));
        if (!entry.icon.empty()) DeleteCopy(File(entry.icon));
        if (!entry.cover.empty()) DeleteCopy(File(entry.cover));
    }
    return OpenResult::Ok;
}

std::vector<fs::path> ChooseFiles(HWND owner, FileKind kind, bool multi) {
    ComPtr<IFileOpenDialog> dialog;
    if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog)))) return {};
    COMDLG_FILTERSPEC filter{Texts::ShortcutType.Get(), L"*.lnk;*.url;*.exe"};
    const wchar_t* title = multi ? Texts::AddShortcutDialog.Get() : Texts::SelectTarget.Get();
    if (kind == FileKind::Icon) {
        filter = {Texts::IconType.Get(), L"*.png;*.ico;*.exe"};
        title = Texts::SelectIcon.Get();
    } else if (kind == FileKind::Cover) {
        filter = {Texts::CoverType.Get(), L"*.png;*.jpg;*.jpeg"};
        title = Texts::SelectCover.Get();
    } else if (kind == FileKind::Backup) {
        filter = {Texts::BackupType.Get(), L"*.polytsia"};
        title = Texts::SelectBackup.Get();
    }
    dialog->SetFileTypes(1, &filter);
    dialog->SetTitle(title);
    FILEOPENDIALOGOPTIONS options = 0;
    dialog->GetOptions(&options);
    dialog->SetOptions(options | FOS_FORCEFILESYSTEM | FOS_FILEMUSTEXIST | FOS_STRICTFILETYPES |
                       FOS_NODEREFERENCELINKS | (multi ? FOS_ALLOWMULTISELECT : 0));
    if (FAILED(dialog->Show(owner))) return {};
    ComPtr<IShellItemArray> results;
    if (FAILED(dialog->GetResults(&results))) return {};
    DWORD count = 0;
    results->GetCount(&count);
    std::vector<fs::path> paths;
    for (DWORD i = 0; i < count; ++i) {
        ComPtr<IShellItem> shellItem;
        if (FAILED(results->GetItemAt(i, &shellItem))) continue;
        PWSTR path = nullptr;
        if (SUCCEEDED(shellItem->GetDisplayName(SIGDN_FILESYSPATH, &path))) {
            paths.emplace_back(path);
            CoTaskMemFree(path);
        }
    }
    return paths;
}

fs::path ChooseBackupDestination(HWND owner) {
    ComPtr<IFileSaveDialog> dialog;
    if (FAILED(CoCreateInstance(CLSID_FileSaveDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog)))) return {};
    const COMDLG_FILTERSPEC filter{Texts::BackupType.Get(), L"*.polytsia"};
    dialog->SetFileTypes(1, &filter);
    dialog->SetDefaultExtension(L"polytsia");
    dialog->SetFileName(Texts::BackupFilename.Get());
    dialog->SetTitle(Texts::SaveBackupDialog.Get());
    FILEOPENDIALOGOPTIONS options = 0;
    dialog->GetOptions(&options);
    dialog->SetOptions(options | FOS_FORCEFILESYSTEM | FOS_OVERWRITEPROMPT | FOS_STRICTFILETYPES);
    ComPtr<IShellItem> item;
    PWSTR path = nullptr;
    if (FAILED(dialog->Show(owner)) || FAILED(dialog->GetResult(&item)) ||
        FAILED(item->GetDisplayName(SIGDN_FILESYSPATH, &path))) return {};
    fs::path result = path;
    CoTaskMemFree(path);
    return result;
}

LaunchResult LaunchShortcut(const fs::path& path, HWND owner, bool elevated) {
    SHELLEXECUTEINFOW request{sizeof(request)};
    request.fMask = SEE_MASK_NOASYNC | SEE_MASK_FLAG_NO_UI;
    request.hwnd = owner;
    request.lpVerb = elevated ? L"runas" : L"open";
    request.lpFile = path.c_str();
    request.nShow = SW_SHOWNORMAL;
    if (ShellExecuteExW(&request)) return LaunchResult::Opened;
    const DWORD error = GetLastError();
    // Declining the UAC prompt is a user choice, not a failure worth reporting.
    if (error == ERROR_CANCELLED) return LaunchResult::Cancelled;
    Log(L"ShellExecuteEx(%s) failed with error %lu", path.filename().c_str(), error);
    return LaunchResult::Failed;
}

bool ShortcutElevatable(const fs::path& path) {
    return Fold(path.extension().wstring()) == L".lnk";
}

fs::path ShortcutTarget(const fs::path& path) {
    if (!ShortcutElevatable(path)) return {};
    ComPtr<IShellLinkW> link;
    ComPtr<IPersistFile> persist;
    if (FAILED(CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&link))) ||
        FAILED(link.As(&persist)) || FAILED(persist->Load(path.c_str(), STGM_READ))) return {};
    link->Resolve(nullptr, SLR_NO_UI | SLR_NOSEARCH | SLR_NOUPDATE | (400u << 16));
    std::wstring target(32768, L'\0');
    if (FAILED(link->GetPath(target.data(), static_cast<int>(target.size()), nullptr, 0))) return {};
    target.resize(wcslen(target.c_str()));
    return target;
}

bool RevealInExplorer(const fs::path& path) {
    if (path.empty() || GetFileAttributesW(path.c_str()) == INVALID_FILE_ATTRIBUTES) return false;
    PIDLIST_ABSOLUTE item = ILCreateFromPathW(path.c_str());
    if (!item) return false;
    const HRESULT result = SHOpenFolderAndSelectItems(item, 0, nullptr, 0);
    ILFree(item);
    if (FAILED(result)) Log(L"SHOpenFolderAndSelectItems failed: 0x%08lX", static_cast<unsigned long>(result));
    return SUCCEEDED(result);
}

namespace {
std::wstring ModulePath() {
    std::wstring path(MAX_PATH, L'\0');
    for (;;) {
        const DWORD length = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
        if (!length) return {};
        if (length < path.size()) {
            path.resize(length);
            return path;
        }
        if (path.size() >= 32768) return {};
        path.resize(path.size() * 2);
    }
}

std::wstring AutoStartCommand() {
    const auto module = ModulePath();
    return module.empty() ? std::wstring{} : L"\"" + module + L"\" --startup";
}

std::wstring RegisteredAutoStart() {
    DWORD bytes = 0;
    if (RegGetValueW(HKEY_CURRENT_USER, RunKey, RunValue, RRF_RT_REG_SZ, nullptr, nullptr, &bytes) != ERROR_SUCCESS ||
        bytes <= sizeof(wchar_t)) return {};
    std::wstring value(bytes / sizeof(wchar_t), L'\0');
    if (RegGetValueW(HKEY_CURRENT_USER, RunKey, RunValue, RRF_RT_REG_SZ, nullptr, value.data(), &bytes) != ERROR_SUCCESS) {
        return {};
    }
    value.resize(wcslen(value.c_str()));
    return value;
}
}

bool AutoStartRegistered() {
    return !RegisteredAutoStart().empty();
}

bool SetAutoStart(bool enabled) {
    if (!enabled) {
        const auto result = RegDeleteKeyValueW(HKEY_CURRENT_USER, RunKey, RunValue);
        const bool ok = result == ERROR_SUCCESS || result == ERROR_FILE_NOT_FOUND || result == ERROR_PATH_NOT_FOUND;
        if (!ok) Log(L"Removing the startup entry failed with error %ld", static_cast<long>(result));
        return ok;
    }
    const auto command = AutoStartCommand();
    if (command.empty()) return false;
    HKEY key = nullptr;
    const auto opened = RegCreateKeyExW(HKEY_CURRENT_USER, RunKey, 0, nullptr, 0, KEY_SET_VALUE,
                                        nullptr, &key, nullptr);
    if (opened != ERROR_SUCCESS) {
        Log(L"Opening the Run key failed with error %ld", static_cast<long>(opened));
        return false;
    }
    const auto result = RegSetValueExW(key, RunValue, 0, REG_SZ, reinterpret_cast<const BYTE*>(command.c_str()),
                                      static_cast<DWORD>((command.size() + 1) * sizeof(wchar_t)));
    RegCloseKey(key);
    if (result != ERROR_SUCCESS) Log(L"Writing the startup entry failed with error %ld", static_cast<long>(result));
    return result == ERROR_SUCCESS;
}

void RefreshAutoStart() {
    // Keep startup working after Polytsia.exe is moved or renamed (for example, after updating to a new release).
    const auto registered = RegisteredAutoStart();
    if (registered.empty()) return;
    const auto expected = AutoStartCommand();
    if (!expected.empty() && CompareStringOrdinal(registered.c_str(), -1, expected.c_str(), -1, TRUE) != CSTR_EQUAL) {
        Log(L"Updating the startup entry to the current location");
        SetAutoStart(true);
    }
}

namespace {
bool SaveUrlShortcut(const std::wstring& url, const fs::path& icon, const fs::path& destination) {
    ComPtr<IUniformResourceLocatorW> link;
    ComPtr<IPersistFile> persist;
    if (FAILED(CoCreateInstance(CLSID_InternetShortcut, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&link))) ||
        FAILED(link.As(&persist)) || FAILED(link->SetURL(url.c_str(), 0))) return false;
    if (!icon.empty()) {
        ComPtr<IPropertySetStorage> properties;
        ComPtr<IPropertyStorage> storage;
        if (SUCCEEDED(link.As(&properties)) &&
            SUCCEEDED(properties->Open(FMTID_Intshcut, STGM_READWRITE, &storage))) {
            PROPSPEC specs[2]{};
            specs[0].ulKind = specs[1].ulKind = PRSPEC_PROPID;
            specs[0].propid = PID_IS_ICONFILE;
            specs[1].propid = PID_IS_ICONINDEX;
            PROPVARIANT values[2]{};
            if (SUCCEEDED(InitPropVariantFromString(icon.c_str(), &values[0]))) {
                InitPropVariantFromInt32(0, &values[1]);
                storage->WriteMultiple(2, specs, values, 0);
                storage->Commit(STGC_DEFAULT);
                PropVariantClear(&values[0]);
            }
        }
    }
    if (FAILED(persist->Save(destination.c_str(), TRUE))) {
        DeleteCopy(destination);
        return false;
    }
    return true;
}

bool SaveExeShortcut(const fs::path& executable, const fs::path& workingDirectory, const fs::path& destination) {
    ComPtr<IShellLinkW> link;
    ComPtr<IPersistFile> persist;
    const auto directory = workingDirectory.empty() ? executable.parent_path() : workingDirectory;
    if (FAILED(CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&link))) ||
        FAILED(link.As(&persist)) || FAILED(link->SetPath(executable.c_str())) ||
        FAILED(link->SetWorkingDirectory(directory.c_str()))) return false;
    if (FAILED(persist->Save(destination.c_str(), TRUE))) {
        DeleteCopy(destination);
        return false;
    }
    return true;
}
}

size_t Library::Import(const std::vector<ImportedGame>& games, size_t& duplicates, size_t& failures) {
    duplicates = failures = 0;
    auto updated = items;
    std::vector<fs::path> created;
    for (const auto& game : games) {
        if (game.source.empty() || Trim(game.name).empty()) { ++failures; continue; }
        if (std::any_of(updated.begin(), updated.end(), [&](const Shortcut& item) {
            return CompareStringOrdinal(item.source.c_str(), -1, game.source.c_str(), -1, TRUE) == CSTR_EQUAL;
        })) {
            ++duplicates;
            continue;
        }
        const auto id = NewId();
        if (id.empty()) { ++failures; continue; }
        const std::wstring file = id + (game.url.empty() ? L".lnk" : L".url");
        const bool saved = game.url.empty() ? SaveExeShortcut(game.executable, game.workingDirectory, File(file))
                                            : SaveUrlShortcut(game.url, game.executable, File(file));
        if (!saved) {
            Log(L"Couldn't create a shortcut for %s", game.name.c_str());
            ++failures;
            continue;
        }
        created.push_back(File(file));
        Shortcut entry{id, Trim(game.name), game.source, file, {}, {}, {}};
        if (!game.cover.empty()) {
            const auto extension = Fold(game.cover.extension().wstring());
            const auto coverId = NewId();
            if (!coverId.empty() && (extension == L".jpg" || extension == L".png") &&
                CopyFileW(game.cover.c_str(), File(coverId + extension).c_str(), TRUE)) {
                entry.cover = coverId + extension;
                created.push_back(File(entry.cover));
            }
        }
        updated.push_back(std::move(entry));
    }
    const size_t added = updated.size() - items.size();
    if (!added) return 0;
    if (!Save(updated, prefs)) {
        for (const auto& file : created) DeleteCopy(file);
        failures += added;
        return 0;
    }
    return added;
}
