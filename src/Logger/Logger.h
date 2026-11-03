#pragma once
#include <Windows.h>
#include <string>

class Logger
{
private:
    static inline HANDLE File = 0;
    static inline std::string ACFolder = {};
    static inline std::string LogFolder = {};

public:
    static bool Initialize();
    static std::string GetLogFolder(bool DoubleSlash = false);
    static bool EjectFileExists();
    static void DeleteEjectFile();
    static bool CreateEjectFile();
    static void Log(const char* Format, ...);
    static void ClearLog();
    static bool Uninitialize();
};
