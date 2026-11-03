#include "Logger.h"
#include <filesystem>

bool Logger::Initialize()
{
    const char* PackageBase = "StudioWildcard.4558480580BB9_1w2mm55455e38";

    char UserProfile[MAX_PATH] = {};
    if (!GetEnvironmentVariableA("USERPROFILE", UserProfile, MAX_PATH))
        ExpandEnvironmentStringsA("%USERPROFILE%", UserProfile, MAX_PATH);

    LogFolder = std::string(UserProfile)
              + "\\AppData\\Local\\Packages\\"
              + PackageBase
               + "\\AC\\paidini\\";

    ACFolder = LogFolder;
    CreateDirectoryA((std::string(UserProfile) + "\\AppData\\Local\\Packages\\" + PackageBase + "\\AC\\paidini").c_str(), nullptr);

    std::string Path = LogFolder + "debug.log";

    for (int attempt = 0; attempt < 10; attempt++)
    {
        File = CreateFileA(Path.c_str(), GENERIC_READ | GENERIC_WRITE,
                           FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                           nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (File != INVALID_HANDLE_VALUE) break;
        Sleep(50);
    }

    OutputDebugStringA(("[paidini] log -> " + Path
                        + " ok=" + std::to_string(File != INVALID_HANDLE_VALUE) + "\n").c_str());
    return File != INVALID_HANDLE_VALUE;
}

std::string Logger::GetLogFolder(bool DoubleSlash)
{
    return LogFolder;
}

bool Logger::EjectFileExists()
{
    return std::filesystem::exists(LogFolder + "eject.tmp");
}

void Logger::DeleteEjectFile()
{
    try { std::filesystem::remove(LogFolder + "eject.tmp"); } catch (...) {}
}

bool Logger::CreateEjectFile()
{
    HANDLE h = CreateFileA((LogFolder + "eject.tmp").c_str(),
                           GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                           FILE_ATTRIBUTE_TEMPORARY, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    CloseHandle(h);
    return true;
}

void Logger::Log(const char* format, ...)
{
    char buf[1024];
    SYSTEMTIME t;
    GetSystemTime(&t);

    int size = GetTimeFormatA(LOCALE_CUSTOM_DEFAULT, 0, &t, "[HH':'mm':'ss] ", buf, sizeof(buf));
    size = (size > 0) ? size - 1 : 0;
    if (size > (int)sizeof(buf) - 2) size = (int)sizeof(buf) - 2;

    va_list args;
    va_start(args, format);
    const int want = vsnprintf(buf + size, sizeof(buf) - size, format, args);
    va_end(args);

    bool truncated = false;
    if (want > 0)
    {
        const int room = (int)sizeof(buf) - 2 - size;
        if (want > room) { size += room; truncated = true; }
        else             { size += want; }
    }
    if (size < 0) size = 0;
    if (size > (int)sizeof(buf) - 2) size = (int)sizeof(buf) - 2;
    buf[size] = '\0';

    if (truncated && size >= 5)
        memcpy(buf + size - 5, "[CUT]", 5);

    OutputDebugStringA(buf);
    OutputDebugStringA("\n");

    if (File && File != INVALID_HANDLE_VALUE)
    {
        buf[size++] = '\n';
        DWORD written;
        WriteFile(File, buf, size, &written, nullptr);
    }
}

void Logger::ClearLog()
{
    if (File && File != INVALID_HANDLE_VALUE)
    {
        SetFilePointer(File, 0, nullptr, FILE_BEGIN);
        SetEndOfFile(File);
    }
    else
    {
        HANDLE h = CreateFileA((LogFolder + "debug.log").c_str(), GENERIC_WRITE, FILE_SHARE_READ,
                               nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (h != INVALID_HANDLE_VALUE) CloseHandle(h);
    }
}

bool Logger::Uninitialize()
{
    if (File && File != INVALID_HANDLE_VALUE)
        return CloseHandle(File);
    return true;
}
