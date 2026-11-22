// Finds installed games in the Steam, Epic Games and GOG libraries so they can be imported as tiles.
// Everything here only reads local launcher files and registry keys; nothing is downloaded.
#include "library.hpp"
#include "log.hpp"
#include <shlobj.h>
#include <algorithm>
#include <fstream>
#include <set>

namespace {
constexpr size_t TextLimit = 4 * 1024 * 1024;

std::string ReadText(const fs::path& path) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) return {};
    const auto size = file.tellg();
    if (size <= 0 || static_cast<uint64_t>(size) > TextLimit) return {};
    std::string text(static_cast<size_t>(size), '\0');
    file.seekg(0);
    if (!file.read(text.data(), size)) return {};
    if (text.size() >= 3 && text.compare(0, 3, "\xEF\xBB\xBF") == 0) text.erase(0, 3);
    return text;
}

std::wstring Wide(const std::string& text) {
    if (text.empty()) return {};
    const int length = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
    if (length <= 0) return {};
    std::wstring result(static_cast<size_t>(length), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), result.data(), length);
    return result;
}

bool IsFile(const fs::path& path) {
    std::error_code error;
    return fs::is_regular_file(path, error);
}

std::wstring RegistryString(HKEY root, const wchar_t* key, const wchar_t* value, REGSAM view = 0) {
    DWORD bytes = 0;
    const DWORD flags = RRF_RT_REG_SZ | (view == KEY_WOW64_32KEY ? RRF_SUBKEY_WOW6432KEY : 0);
    if (RegGetValueW(root, key, value, flags, nullptr, nullptr, &bytes) != ERROR_SUCCESS || bytes <= sizeof(wchar_t)) {
        return {};
    }
    std::wstring text(bytes / sizeof(wchar_t), L'\0');
    if (RegGetValueW(root, key, value, flags, nullptr, text.data(), &bytes) != ERROR_SUCCESS) return {};
    text.resize(wcslen(text.c_str()));
    return text;
}

// --- Steam ---------------------------------------------------------------------------------------

// Key/value pairs from Valve's KeyValues text format (libraryfolders.vdf, appmanifest_*.acf).
struct VdfPair {
    std::string key, value;
    int depth;
};

std::vector<VdfPair> ParseVdf(const std::string& text) {
    std::vector<VdfPair> pairs;
    std::string key;
    bool haveKey = false;
    int depth = 0;
    for (size_t i = 0; i < text.size(); ++i) {
        const char ch = text[i];
        if (ch == '{') { ++depth; haveKey = false; continue; }
        if (ch == '}') { depth = std::max(0, depth - 1); haveKey = false; continue; }
        if (ch == '/' && i + 1 < text.size() && text[i + 1] == '/') {
            while (i < text.size() && text[i] != '\n') ++i;
            continue;
        }
        if (ch != '"') continue;
        std::string token;
        for (++i; i < text.size() && text[i] != '"'; ++i) {
            if (text[i] == '\\' && i + 1 < text.size()) {
                const char next = text[++i];
                token += next == 'n' ? '\n' : next == 't' ? '\t' : next;
            } else token += text[i];
        }
        if (!haveKey) {
            key = std::move(token);
            haveKey = true;
        } else {
            pairs.push_back({std::move(key), std::move(token), depth});
            haveKey = false;
        }
    }
    return pairs;
}

std::string VdfValue(const std::vector<VdfPair>& pairs, const char* key, int depth) {
    for (const auto& pair : pairs) {
        if (pair.depth == depth && _stricmp(pair.key.c_str(), key) == 0) return pair.value;
    }
    return {};
}

fs::path SteamRoot() {
    auto path = RegistryString(HKEY_CURRENT_USER, L"Software\\Valve\\Steam", L"SteamPath");
    if (path.empty()) path = RegistryString(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Valve\\Steam", L"InstallPath", KEY_WOW64_32KEY);
    if (path.empty()) return {};
    return fs::path(path).make_preferred();
}

fs::path SteamCover(const fs::path& steam, const std::wstring& appId) {
    const auto cache = steam / L"appcache" / L"librarycache";
    for (const auto& candidate : {cache / (appId + L"_library_600x900.jpg"), cache / appId / L"library_600x900.jpg"}) {
        if (IsFile(candidate)) return candidate;
    }
    // Newer Steam clients keep artwork in hashed subfolders of librarycache/<appid>.
    std::error_code error;
    fs::recursive_directory_iterator it(cache / appId, fs::directory_options::skip_permission_denied, error), end;
    for (; !error && it != end; it.increment(error)) {
        if (it.depth() > 2) { it.disable_recursion_pending(); continue; }
        const auto name = it->path().filename().wstring();
        if (it->is_regular_file(error) && name.rfind(L"library_600x900", 0) == 0) return it->path();
    }
    return {};
}

bool SteamTool(const std::string& appId, const std::string& name) {
    static const char* const ToolNames[] = {"Proton", "Steam Linux Runtime", "Steamworks Common Redistributables",
                                            "SteamVR", "Steam Audio"};
    if (appId == "228980") return true;
    return std::any_of(std::begin(ToolNames), std::end(ToolNames),
                       [&](const char* tool) { return name.rfind(tool, 0) == 0; });
}

void FindSteamGames(std::vector<ImportedGame>& games) {
    const auto steam = SteamRoot();
    if (steam.empty()) return;
    std::vector<fs::path> libraries{steam};
    std::set<std::wstring> seen{Fold(steam.wstring())};
    for (const auto& pair : ParseVdf(ReadText(steam / L"steamapps" / L"libraryfolders.vdf"))) {
        if (_stricmp(pair.key.c_str(), "path") != 0) continue;
        const auto library = fs::path(Wide(pair.value)).make_preferred();
        if (seen.insert(Fold(library.wstring())).second) libraries.push_back(library);
    }
    for (const auto& library : libraries) {
        std::error_code error;
        for (fs::directory_iterator it(library / L"steamapps", error), end; !error && it != end; it.increment(error)) {
            const auto filename = it->path().filename().wstring();
            if (filename.rfind(L"appmanifest_", 0) != 0 || Fold(it->path().extension().wstring()) != L".acf") continue;
            const auto manifest = ParseVdf(ReadText(it->path()));
            const auto appId = VdfValue(manifest, "appid", 1);
            const auto name = VdfValue(manifest, "name", 1);
            const auto flags = std::atoi(VdfValue(manifest, "StateFlags", 1).c_str());
            if (appId.empty() || name.empty() || !(flags & 4) || SteamTool(appId, name)) continue;
            if (appId.find_first_not_of("0123456789") != std::string::npos) continue;
            const auto id = Wide(appId);
            ImportedGame game;
            game.name = Wide(name);
            game.url = game.source = L"steam://rungameid/" + id;
            game.cover = SteamCover(steam, id);
            games.push_back(std::move(game));
        }
    }
}

// --- Epic Games ----------------------------------------------------------------------------------

// Returns the string value of "key" from a flat JSON object (Epic .item manifests are flat for these keys).
std::string JsonString(const std::string& json, const char* key) {
    const std::string needle = std::string("\"") + key + "\"";
    size_t at = json.find(needle);
    if (at == std::string::npos) return {};
    at = json.find_first_not_of(" \t\r\n", at + needle.size());
    if (at == std::string::npos || json[at] != ':') return {};
    at = json.find_first_not_of(" \t\r\n", at + 1);
    if (at == std::string::npos || json[at] != '"') return {};
    std::string value;
    for (++at; at < json.size() && json[at] != '"'; ++at) {
        if (json[at] != '\\' || at + 1 >= json.size()) { value += json[at]; continue; }
        const char next = json[++at];
        if (next == 'u' && at + 4 < json.size()) {
            const auto code = static_cast<unsigned>(std::strtoul(json.substr(at + 1, 4).c_str(), nullptr, 16));
            at += 4;
            if (code < 0x80) value += static_cast<char>(code);
            else if (code < 0x800) {
                value += static_cast<char>(0xC0 | (code >> 6));
                value += static_cast<char>(0x80 | (code & 0x3F));
            } else {
                value += static_cast<char>(0xE0 | (code >> 12));
                value += static_cast<char>(0x80 | ((code >> 6) & 0x3F));
                value += static_cast<char>(0x80 | (code & 0x3F));
            }
        } else value += next == 'n' ? '\n' : next == 't' ? '\t' : next;
    }
    return value;
}

bool JsonTrue(const std::string& json, const char* key) {
    const std::string needle = std::string("\"") + key + "\"";
    const size_t at = json.find(needle);
    if (at == std::string::npos) return false;
    const size_t value = json.find_first_not_of(" \t\r\n:", at + needle.size());
    return value != std::string::npos && json.compare(value, 4, "true") == 0;
}

void FindEpicGames(std::vector<ImportedGame>& games) {
    PWSTR folder = nullptr;
    if (FAILED(SHGetKnownFolderPath(FOLDERID_ProgramData, 0, nullptr, &folder))) return;
    const auto manifests = fs::path(folder) / L"Epic" / L"EpicGamesLauncher" / L"Data" / L"Manifests";
    CoTaskMemFree(folder);
    std::error_code error;
    for (fs::directory_iterator it(manifests, error), end; !error && it != end; it.increment(error)) {
        if (Fold(it->path().extension().wstring()) != L".item") continue;
        const auto json = ReadText(it->path());
        const auto appName = JsonString(json, "AppName");
        const auto mainApp = JsonString(json, "MainGameAppName");
        const auto catalogNamespace = JsonString(json, "CatalogNamespace");
        const auto catalogItem = JsonString(json, "CatalogItemId");
        const auto executable = JsonString(json, "LaunchExecutable");
        if (appName.empty() || catalogNamespace.empty() || catalogItem.empty() || executable.empty() ||
            JsonTrue(json, "bIsIncompleteInstall") || (!mainApp.empty() && mainApp != appName)) continue;
        ImportedGame game;
        game.name = Wide(JsonString(json, "DisplayName"));
        game.url = game.source = L"com.epicgames.launcher://apps/" + Wide(catalogNamespace) + L"%3A" +
            Wide(catalogItem) + L"%3A" + Wide(appName) + L"?action=launch&silent=true";
        const auto icon = (fs::path(Wide(JsonString(json, "InstallLocation"))) / Wide(executable)).make_preferred();
        if (IsFile(icon)) game.executable = icon;
        if (!game.name.empty()) games.push_back(std::move(game));
    }
}

// --- GOG -----------------------------------------------------------------------------------------

void FindGogGames(std::vector<ImportedGame>& games) {
    constexpr wchar_t GamesKey[] = L"SOFTWARE\\GOG.com\\Games";
    HKEY root = nullptr;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, GamesKey, 0, KEY_READ | KEY_WOW64_32KEY, &root) != ERROR_SUCCESS) return;
    wchar_t name[256];
    for (DWORD index = 0;; ++index) {
        DWORD length = ARRAYSIZE(name);
        if (RegEnumKeyExW(root, index, name, &length, nullptr, nullptr, nullptr, nullptr) != ERROR_SUCCESS) break;
        const std::wstring key = std::wstring(GamesKey) + L"\\" + name;
        // Entries with dependsOn are DLC for another game.
        if (!RegistryString(HKEY_LOCAL_MACHINE, key.c_str(), L"dependsOn", KEY_WOW64_32KEY).empty()) continue;
        const fs::path executable = RegistryString(HKEY_LOCAL_MACHINE, key.c_str(), L"exe", KEY_WOW64_32KEY);
        if (Fold(executable.extension().wstring()) != L".exe" || !IsFile(executable)) continue;
        ImportedGame game;
        game.name = RegistryString(HKEY_LOCAL_MACHINE, key.c_str(), L"gameName", KEY_WOW64_32KEY);
        if (game.name.empty()) game.name = executable.stem().wstring();
        game.executable = executable;
        game.source = executable.lexically_normal().wstring();
        game.workingDirectory = RegistryString(HKEY_LOCAL_MACHINE, key.c_str(), L"workingDir", KEY_WOW64_32KEY);
        games.push_back(std::move(game));
    }
    RegCloseKey(root);
}
}

std::vector<ImportedGame> FindInstalledGames() {
    std::vector<ImportedGame> games;
    FindSteamGames(games);
    FindEpicGames(games);
    FindGogGames(games);
    std::sort(games.begin(), games.end(), [](const ImportedGame& a, const ImportedGame& b) {
        return CompareStringEx(LOCALE_NAME_USER_DEFAULT, LINGUISTIC_IGNORECASE, a.name.c_str(), -1,
                               b.name.c_str(), -1, nullptr, nullptr, 0) == CSTR_LESS_THAN;
    });
    return games;
}
