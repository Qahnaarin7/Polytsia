#include "log.hpp"
#include <windows.h>
#include <cstdarg>
#include <cwchar>
#include <mutex>
#include <string>

namespace {
constexpr uintmax_t LogLimit = 512 * 1024;
std::mutex logMutex;
std::filesystem::path logFile;
}

void InitLog(const std::filesystem::path& directory) {
    std::lock_guard lock(logMutex);
    logFile = directory.empty() ? std::filesystem::path{} : directory / L"log.txt";
}

void Log(const wchar_t* format, ...) {
    wchar_t message[1024]{};
    va_list arguments;
    va_start(arguments, format);
    _vsnwprintf_s(message, ARRAYSIZE(message), _TRUNCATE, format, arguments);
    va_end(arguments);

    SYSTEMTIME now{};
    GetLocalTime(&now);
    wchar_t line[1100]{};
    _snwprintf_s(line, ARRAYSIZE(line), _TRUNCATE, L"%04u-%02u-%02u %02u:%02u:%02u  %s\r\n",
                 now.wYear, now.wMonth, now.wDay, now.wHour, now.wMinute, now.wSecond, message);
    OutputDebugStringW(line);

    std::lock_guard lock(logMutex);
    if (logFile.empty()) return;
    std::error_code error;
    if (std::filesystem::file_size(logFile, error) > LogLimit && !error) {
        auto old = logFile;
        old.replace_filename(L"log.old.txt");
        MoveFileExW(logFile.c_str(), old.c_str(), MOVEFILE_REPLACE_EXISTING);
    }
    const int bytes = WideCharToMultiByte(CP_UTF8, 0, line, -1, nullptr, 0, nullptr, nullptr);
    if (bytes <= 1) return;
    std::string utf8(static_cast<size_t>(bytes - 1), '\0');
    WideCharToMultiByte(CP_UTF8, 0, line, -1, utf8.data(), bytes, nullptr, nullptr);
    HANDLE file = CreateFileW(logFile.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                              OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return;
    DWORD written = 0;
    WriteFile(file, utf8.data(), static_cast<DWORD>(utf8.size()), &written, nullptr);
    CloseHandle(file);
}
