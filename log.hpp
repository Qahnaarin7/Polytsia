#pragma once
#include <filesystem>

// Diagnostic log in the data folder (log.txt). Rotates to log.old.txt at 512 KB.
void InitLog(const std::filesystem::path& directory);
void Log(const wchar_t* format, ...);
