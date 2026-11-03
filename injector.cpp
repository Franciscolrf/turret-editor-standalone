// ============================================================================
//  paidini - manual map injector
//  Reverse-engineered method from "the sick injecter.exe":
//  - RtlAddFunctionTable  (registers .pdata so C++ exceptions don't crash)
//  - NtCreateThreadEx     (stealth thread, hidden from debugger)
//  - AppContainer SID     (grants DLL file access to MS Store process)
//  - SeDebugPrivilege     (opens protected UWP-hosted process)
// ============================================================================

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <TlHelp32.h>
#include <sddl.h>       // ConvertStringSidToSid
#include <aclapi.h>     // SetNamedSecurityInfo
#include <winternl.h>
#include <cstdio>
#include <cstring>
#include <vector>
#include <string>
#include <conio.h>      // _getch, _kbhit

#pragma comment(lib, "kernel32.lib")
#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "ntdll.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")

// ---------------------------------------------------------------------------
// GUI state (separate window, no console)
// ---------------------------------------------------------------------------
static HWND  g_hMain   = nullptr;
static HWND  g_hStatus = nullptr;
static HWND  g_hLogList = nullptr;
static HWND  g_hCloseBtn = nullptr;
static HFONT g_hFont  = nullptr;
static HFONT g_hFontBold = nullptr;
static HBRUSH g_hBgBrush = nullptr;

static int   g_state = 0; // 0=waiting 1=injecting 2=injected 3=failed
static char  g_resultLine[64] = "pid ----";
static char  g_statusLine[128] = "waiting for ARK...";
static std::string g_dllPathA;   // display only (converted from wide)
static std::wstring g_dllPathW;  // real path for file ops (Unicode-safe)
static bool  g_useColor = true;  // false on old Win10 conhost without VT

static std::string WideToUtf8(const std::wstring& w)
{
    if (w.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (n <= 0) return {};
    std::string r((size_t)n - 1, 0);
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, &r[0], n, nullptr, nullptr);
    return r;
}

static void Log(const char* prefix, const char* msg, DWORD err);

// ---------------------------------------------------------------------------
// NtCreateThreadEx (undocumented)
// ---------------------------------------------------------------------------
typedef NTSTATUS(NTAPI* NtCreateThreadEx_t)(
    PHANDLE             ThreadHandle,
    ACCESS_MASK         DesiredAccess,
    PVOID               ObjectAttributes,
    HANDLE              ProcessHandle,
    PVOID               StartRoutine,
    PVOID               Argument,
    ULONG               CreateFlags,         // 0x4 = hide from debugger
    SIZE_T              ZeroBits,
    SIZE_T              StackSize,
    SIZE_T              MaximumStackSize,
    PVOID               AttributeList
);

#define THREAD_CREATE_FLAGS_HIDE_FROM_DEBUGGER  0x4
#define THREAD_CREATE_FLAGS_SKIP_THREAD_ATTACH  0x2

// ---------------------------------------------------------------------------
// NtQueryInformationProcess (for AppContainer token SID)
// ---------------------------------------------------------------------------
typedef NTSTATUS(NTAPI* NtQueryInformationProcess_t)(
    HANDLE              ProcessHandle,
    PROCESSINFOCLASS    ProcessInformationClass,
    PVOID               ProcessInformation,
    ULONG               ProcessInformationLength,
    PULONG              ReturnLength
);
#define ProcessBasicInformation 0

// ---------------------------------------------------------------------------
// Loader data placed in target alongside shellcode
// ---------------------------------------------------------------------------
struct LOADER_DATA
{
    // Win32 functions resolved in injector, passed in for shellcode use
    HMODULE (WINAPI* fnLoadLibraryA)(LPCSTR);
    FARPROC (WINAPI* fnGetProcAddress)(HMODULE, LPCSTR);
    BOOLEAN (WINAPI* fnRtlAddFunctionTable)(PVOID, DWORD, DWORD64); // registers .pdata

    void*    pBase;     // mapped image base in target
    LONGLONG delta;     // reloc delta

    // written back by shellcode
    volatile DWORD step;
    volatile DWORD lastErr;
};

// ---------------------------------------------------------------------------
// Shellcode — fully PIC, runs in target process
// ---------------------------------------------------------------------------
#pragma optimize("", off)
#pragma runtime_checks("", off)
#pragma check_stack(off)

static DWORD WINAPI LoaderShellcode(LOADER_DATA* d)
{
    d->step = 1;

    BYTE* base = (BYTE*)d->pBase;
    auto* dos  = (IMAGE_DOS_HEADER*)base;
    auto* nt   = (IMAGE_NT_HEADERS*)(base + dos->e_lfanew);

    // ---- Relocations -------------------------------------------------------
    d->step = 2;
    auto& relocDir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_BASERELOC];
    if (d->delta && relocDir.Size)
    {
        auto* block = (IMAGE_BASE_RELOCATION*)(base + relocDir.VirtualAddress);
        while (block->VirtualAddress && block->SizeOfBlock >= sizeof(IMAGE_BASE_RELOCATION))
        {
            DWORD n       = (block->SizeOfBlock - sizeof(IMAGE_BASE_RELOCATION)) / sizeof(WORD);
            WORD* entries = (WORD*)(block + 1);
            for (DWORD i = 0; i < n; i++)
            {
                int type   = entries[i] >> 12;
                int offset = entries[i] & 0x0FFF;
                if (type == IMAGE_REL_BASED_DIR64)
                    *(ULONGLONG*)(base + block->VirtualAddress + offset) += (ULONGLONG)d->delta;
                else if (type == IMAGE_REL_BASED_HIGHLOW)
                    *(DWORD*)(base + block->VirtualAddress + offset) += (DWORD)d->delta;
            }
            block = (IMAGE_BASE_RELOCATION*)((BYTE*)block + block->SizeOfBlock);
        }
    }

    // ---- Imports -----------------------------------------------------------
    d->step = 3;
    auto& importDir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (importDir.Size)
    {
        auto* desc = (IMAGE_IMPORT_DESCRIPTOR*)(base + importDir.VirtualAddress);
        for (; desc->Name; desc++)
        {
            HMODULE hMod = d->fnLoadLibraryA((LPCSTR)(base + desc->Name));
            if (!hMod) continue;

            auto* orig  = (IMAGE_THUNK_DATA*)(base + desc->OriginalFirstThunk);
            auto* thunk = (IMAGE_THUNK_DATA*)(base + desc->FirstThunk);

            for (; orig->u1.AddressOfData; orig++, thunk++)
            {
                FARPROC fn;
                if (IMAGE_SNAP_BY_ORDINAL(orig->u1.Ordinal))
                    fn = d->fnGetProcAddress(hMod, (LPCSTR)(orig->u1.Ordinal & 0xFFFF));
                else
                {
                    auto* ibn = (IMAGE_IMPORT_BY_NAME*)(base + orig->u1.AddressOfData);
                    fn = d->fnGetProcAddress(hMod, ibn->Name);
                }
                thunk->u1.Function = (ULONG_PTR)fn;
            }
        }
    }

    // ---- Exception table (RtlAddFunctionTable) -----------------------------
    // Critical for MS Store / C++ builds: without this, any exception or
    // MSVC runtime SEH unwind inside the injected DLL crashes the process.
    d->step = 4;
    auto& excDir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXCEPTION];
    if (excDir.Size && d->fnRtlAddFunctionTable)
    {
        d->fnRtlAddFunctionTable(
            (PVOID)(base + excDir.VirtualAddress),
            excDir.Size / sizeof(IMAGE_RUNTIME_FUNCTION_ENTRY),
            (DWORD64)base);
    }

    // ---- TLS callbacks -----------------------------------------------------
    d->step = 5;
    auto& tlsDir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_TLS];
    if (tlsDir.Size)
    {
        auto* tls = (IMAGE_TLS_DIRECTORY*)(base + tlsDir.VirtualAddress);
        auto* cb  = (PIMAGE_TLS_CALLBACK*)tls->AddressOfCallBacks;
        if (cb)
            for (; *cb; cb++)
                (*cb)((PVOID)base, DLL_PROCESS_ATTACH, nullptr);
    }

    // ---- DllMain -----------------------------------------------------------
    d->step = 6;
    if (nt->OptionalHeader.AddressOfEntryPoint)
    {
        using DllMain_t = BOOL(WINAPI*)(HINSTANCE, DWORD, LPVOID);
        auto entry = (DllMain_t)(base + nt->OptionalHeader.AddressOfEntryPoint);
        entry((HINSTANCE)base, DLL_PROCESS_ATTACH, nullptr);
    }

    d->step = 7;
    return 0;
}

#pragma check_stack()
#pragma runtime_checks("", restore)
#pragma optimize("", on)

// ---------------------------------------------------------------------------
// Persistent logger — paidini/debug.log
// ---------------------------------------------------------------------------
static std::wstring  g_paidiniDir;
static HANDLE        g_logHandle = INVALID_HANDLE_VALUE;

static std::wstring GetPaidiniDir()
{
    wchar_t local[MAX_PATH] = {};
    DWORD n = GetEnvironmentVariableW(L"LOCALAPPDATA", local, MAX_PATH);
    if (n == 0 || n >= MAX_PATH)
    {
        // Fallback for stripped/service envs: USERPROFILE\AppData\Local
        wchar_t profile[MAX_PATH] = {};
        DWORD m = GetEnvironmentVariableW(L"USERPROFILE", profile, MAX_PATH);
        if (m > 0 && m < MAX_PATH)
            swprintf_s(local, L"%s\\AppData\\Local", profile);
        else
            wcscpy_s(local, L"C:\\Temp");
        Log("[!]", "LOCALAPPDATA missing, fell back", GetLastError());
    }
    std::wstring r = local;
    r += L"\\Packages\\StudioWildcard.4558480580BB9_1w2mm55455e38\\AC\\paidini";
    return r;
}

static void CreateDirIfNeeded(const std::wstring& path)
{
    CreateDirectoryW(path.c_str(), nullptr);
    // ERROR_ALREADY_EXISTS is fine — we don't care
}

static void EnsureFolders()
{
    g_paidiniDir = GetPaidiniDir();

    // create paidini and paidini/config
    CreateDirIfNeeded(g_paidiniDir);
    CreateDirIfNeeded(g_paidiniDir + L"\\config");

    // default config.json — only written on first run (CREATE_NEW)
    std::wstring cfgPath = g_paidiniDir + L"\\config\\config.json";
    HANDLE hCfg = CreateFileW(cfgPath.c_str(), GENERIC_WRITE, 0, nullptr,
                              CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hCfg != INVALID_HANDLE_VALUE)
    {
        const char* defaults =
            "{\n"
            "    \"version\": \"1.0\",\n"
            "    \"dll_path\": \"\",\n"
            "    \"auto_inject\": false,\n"
            "    \"wait_timeout_ms\": 8000\n"
            "}\n";
        DWORD written = 0;
        WriteFile(hCfg, defaults, (DWORD)strlen(defaults), &written, nullptr);
        CloseHandle(hCfg);
    }

    // debug.log — open for append via Win32; creates if not present.
    // NOTE: full sharing — the injected DLL's Logger writes to this same file.
    std::wstring logPath = g_paidiniDir + L"\\debug.log";
    g_logHandle = CreateFileW(logPath.c_str(),
                              FILE_APPEND_DATA,
                              FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                              nullptr,
                              OPEN_ALWAYS,
                              FILE_ATTRIBUTE_NORMAL,
                              nullptr);
}

static void Log(const char* prefix, const char* msg, DWORD err = 0)
{
    SYSTEMTIME st;
    GetLocalTime(&st);

    char line[1024];
    int n;
    if (err)
    {
        // Standard Windows error: "error 5: Access is denied" + hex for search.
        char errMsg[256] = {};
        DWORD flags = FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS;
        DWORD cch = FormatMessageA(flags, nullptr, err, 0, errMsg, (DWORD)sizeof(errMsg), nullptr);
        while (cch && (errMsg[cch-1] == '\n' || errMsg[cch-1] == '\r' || errMsg[cch-1] == '.'))
            errMsg[--cch] = 0;
        if (!cch) sprintf_s(errMsg, "unknown error");
        n = sprintf_s(line, sizeof(line), "[%02d:%02d:%02d.%03d] %s %s  (error %lu: %s, 0x%08X)\n",
                      st.wHour, st.wMinute, st.wSecond, st.wMilliseconds, prefix, msg, (unsigned long)err, errMsg, err);
    }
    else
        n = sprintf_s(line, sizeof(line), "[%02d:%02d:%02d.%03d] %s %s\n",
                      st.wHour, st.wMinute, st.wSecond, st.wMilliseconds, prefix, msg);

    if (g_logHandle != INVALID_HANDLE_VALUE && n > 0)
    {
        DWORD written;
        WriteFile(g_logHandle, line, (DWORD)n, &written, nullptr);
    }
    // file-only - UI stays clean like Admin Tool, plain lines via GuiLinePlain
}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static bool EnableDebugPrivilege()
{
    HANDLE hToken;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &hToken))
        return false;
    LUID luid;
    LookupPrivilegeValueA(nullptr, "SeDebugPrivilege", &luid);
    TOKEN_PRIVILEGES tp = { 1, { luid, SE_PRIVILEGE_ENABLED } };
    bool ok = AdjustTokenPrivileges(hToken, FALSE, &tp, sizeof(tp), nullptr, nullptr)
              && GetLastError() == ERROR_SUCCESS;
    CloseHandle(hToken);
    return ok;
}

static DWORD FindProcess(const std::vector<std::wstring>& names)
{
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return 0;
    PROCESSENTRY32W pe = { sizeof(pe) };
    DWORD pid = 0;
    if (Process32FirstW(snap, &pe))
        do {
            for (auto& n : names)
                if (_wcsicmp(pe.szExeFile, n.c_str()) == 0) { pid = pe.th32ProcessID; break; }
        } while (!pid && Process32NextW(snap, &pe));
    CloseHandle(snap);
    return pid;
}

// ---------------------------------------------------------------------------
// Grant DLL file access to the process AppContainer SID (MS Store)
// Without this the AppContainer process may not be able to map dependencies.
// ---------------------------------------------------------------------------
static void GrantAppContainerAccess(HANDLE hProcess, const std::wstring& dllPath)
{
    HANDLE hToken = nullptr;
    if (!OpenProcessToken(hProcess, TOKEN_QUERY, &hToken)) return;

    // Query the AppContainer SID from the process token
    DWORD retLen = 0;
    GetTokenInformation(hToken, TokenAppContainerSid, nullptr, 0, &retLen);
    if (!retLen) { CloseHandle(hToken); return; }

    std::vector<BYTE> buf(retLen);
    if (!GetTokenInformation(hToken, TokenAppContainerSid, buf.data(), retLen, &retLen))
    {
        CloseHandle(hToken); return;
    }
    CloseHandle(hToken);

    auto* acSid = reinterpret_cast<TOKEN_APPCONTAINER_INFORMATION*>(buf.data());
    if (!acSid->TokenAppContainer) return; // not an AppContainer process

    Log("[*]", "AppContainer SID found - granting DLL file access...");

    EXPLICIT_ACCESS_W ea = {};
    ea.grfAccessPermissions = GENERIC_READ | GENERIC_EXECUTE;
    ea.grfAccessMode        = GRANT_ACCESS;
    ea.grfInheritance       = NO_INHERITANCE;
    ea.Trustee.TrusteeForm  = TRUSTEE_IS_SID;
    ea.Trustee.TrusteeType  = TRUSTEE_IS_UNKNOWN;
    ea.Trustee.ptstrName    = reinterpret_cast<LPWCH>(acSid->TokenAppContainer);

    PACL pNewAcl = nullptr;
    if (SetEntriesInAclW(1, &ea, nullptr, &pNewAcl) == ERROR_SUCCESS)
    {
        SetNamedSecurityInfoW(
            const_cast<LPWSTR>(dllPath.c_str()),
            SE_FILE_OBJECT,
            DACL_SECURITY_INFORMATION,
            nullptr, nullptr, pNewAcl, nullptr);
        LocalFree(pNewAcl);
        Log("[+]", "AppContainer access granted.");
    }
}

// ---------------------------------------------------------------------------
// Create remote thread via NtCreateThreadEx (stealth, hidden from debugger)
// Falls back to CreateRemoteThread if ntdll export is missing
// ---------------------------------------------------------------------------
static HANDLE CreateStealtyhThread(HANDLE hProcess, LPVOID pStart, LPVOID pArg)
{
    auto fnNtCreateThreadEx = reinterpret_cast<NtCreateThreadEx_t>(
        GetProcAddress(GetModuleHandleA("ntdll.dll"), "NtCreateThreadEx"));

    HANDLE hThread = nullptr;

    if (fnNtCreateThreadEx)
    {
        NTSTATUS status = fnNtCreateThreadEx(
            &hThread,
            THREAD_ALL_ACCESS,
            nullptr,
            hProcess,
            pStart,
            pArg,
            THREAD_CREATE_FLAGS_HIDE_FROM_DEBUGGER,
            0, 0, 0, nullptr);

        if (status == 0 && hThread)
        {
            Log("[+]", "NtCreateThreadEx stealth thread created.");
            return hThread;
        }
        char buf[64]; sprintf_s(buf, "NtCreateThreadEx failed (0x%X), falling back", status);
        Log("[!]", buf);
    }

    hThread = CreateRemoteThread(hProcess, nullptr, 0,
        reinterpret_cast<LPTHREAD_START_ROUTINE>(pStart), pArg, 0, nullptr);
    if (hThread) Log("[+]", "CreateRemoteThread fallback used.");
    return hThread;
}

// ---------------------------------------------------------------------------
// Manual map core
// ---------------------------------------------------------------------------
static BYTE* ManualMap(HANDLE hProc, const std::wstring& dllPathW)
{
    // ---- Read DLL (Unicode-safe) -------------------------------------------
    { Log("[*]", ("Reading: " + WideToUtf8(dllPathW)).c_str()); }
    HANDLE hFile = CreateFileW(dllPathW.c_str(), GENERIC_READ, FILE_SHARE_READ,
                               nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hFile == INVALID_HANDLE_VALUE) { Log("[-]", "Cannot open DLL", GetLastError()); return nullptr; }
    LARGE_INTEGER liSz; GetFileSizeEx(hFile, &liSz);
    size_t sz = (size_t)liSz.QuadPart;
    std::vector<BYTE> raw(sz);
    DWORD bytesRead = 0;
    if (!ReadFile(hFile, raw.data(), (DWORD)sz, &bytesRead, nullptr) || bytesRead != (DWORD)sz)
        { CloseHandle(hFile); Log("[-]", "Read failed", GetLastError()); return nullptr; }
    CloseHandle(hFile);
    { char buf[64]; sprintf_s(buf, "Read %zu bytes", sz); Log("[+]", buf); }

    // ---- Validate PE -------------------------------------------------------
    auto* dos = (IMAGE_DOS_HEADER*)raw.data();
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) { Log("[-]", "Bad DOS magic"); return nullptr; }
    auto* nt = (IMAGE_NT_HEADERS*)(raw.data() + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE)   { Log("[-]", "Bad NT sig");    return nullptr; }
    if (nt->FileHeader.Machine != IMAGE_FILE_MACHINE_AMD64) { Log("[-]", "Not x64"); return nullptr; }

    DWORD    imgSize  = nt->OptionalHeader.SizeOfImage;
    LONGLONG prefBase = (LONGLONG)nt->OptionalHeader.ImageBase;
    { char buf[64]; sprintf_s(buf, "Image size: %u bytes", imgSize); Log("[+]", buf); }

    // ---- Allocate image ----------------------------------------------------
    Log("[*]", "Allocating image in target...");
    BYTE* remBase = (BYTE*)VirtualAllocEx(hProc, (LPVOID)prefBase, imgSize,
                                          MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    if (!remBase)
    {
        Log("[!]", "Preferred base busy, trying any address...");
        remBase = (BYTE*)VirtualAllocEx(hProc, nullptr, imgSize,
                                        MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    }
    if (!remBase) { Log("[-]", "VirtualAllocEx failed", GetLastError()); return nullptr; }
    { char buf[64]; sprintf_s(buf, "Image base: 0x%llX", (ULONGLONG)remBase); Log("[+]", buf); }

    LONGLONG delta = (LONGLONG)remBase - prefBase;
    { char buf[64]; sprintf_s(buf, "Reloc delta: 0x%llX", (ULONGLONG)delta); Log("[*]", buf); }

    // ---- Map sections locally then write -----------------------------------
    Log("[*]", "Mapping sections...");
    { char buf[64]; sprintf_s(buf, "  file size: %zu  image size: %u", sz, imgSize); Log("[*]", buf); }
    { char buf[64]; sprintf_s(buf, "  SizeOfHeaders: %u  NumSections: %u", nt->OptionalHeader.SizeOfHeaders, nt->FileHeader.NumberOfSections); Log("[*]", buf); }

    Log("[*]", "  allocating mapped buffer...");
    std::vector<BYTE> mapped(imgSize, 0);
    Log("[*]", "  mapped buffer OK, copying headers...");

    if (nt->OptionalHeader.SizeOfHeaders > sz)
    { Log("[-]", "  SizeOfHeaders > file size - clamping"); }
    DWORD hdrCopy = nt->OptionalHeader.SizeOfHeaders < (DWORD)sz ? nt->OptionalHeader.SizeOfHeaders : (DWORD)sz;
    memcpy(mapped.data(), raw.data(), hdrCopy);
    Log("[*]", "  headers copied, entering section loop...");

    // Log raw pointers to catch any bad-address crash
    { char buf[128]; sprintf_s(buf, "  raw.data()=0x%p  nt=0x%p  e_lfanew=0x%X",
        (void*)raw.data(), (void*)nt, dos->e_lfanew); Log("[*]", buf); }

    auto* sec = IMAGE_FIRST_SECTION(nt);
    { char buf[64]; sprintf_s(buf, "  first sec ptr=0x%p  sections=%u", (void*)sec, nt->FileHeader.NumberOfSections); Log("[*]", buf); }

    BYTE* rawStart = raw.data();
    BYTE* rawEnd   = rawStart + sz;

    for (WORD i = 0; i < nt->FileHeader.NumberOfSections; i++, sec++)
    {
        { char buf[48]; sprintf_s(buf, "  loop i=%u sec=0x%p", i, (void*)sec); Log("[*]", buf); }

        // bounds check the section header pointer before dereferencing
        if ((BYTE*)sec < rawStart || (BYTE*)sec + sizeof(IMAGE_SECTION_HEADER) > rawEnd)
        {
            char ebuf[80]; sprintf_s(ebuf, "  [%u] sec 0x%p outside raw [0x%p - 0x%p]",
                i, (void*)sec, (void*)rawStart, (void*)rawEnd);
            Log("[-]", ebuf);
            break;
        }

        char sname[80];
        sprintf_s(sname, "  [%u] %.8s  VA:0x%X  RawOff:0x%X  RawSz:0x%X  VirtSz:0x%X",
            i, (char*)sec->Name,
            sec->VirtualAddress,
            sec->PointerToRawData,
            sec->SizeOfRawData,
            sec->Misc.VirtualSize);
        Log("   ", sname);

        if (!sec->SizeOfRawData) { Log("   ", "    -> skipped (no raw data)"); continue; }

        // bounds: don't read past end of file
        if ((SIZE_T)sec->PointerToRawData + sec->SizeOfRawData > sz)
        {
            Log("[-]", "    -> SKIP: raw data reads past EOF");
            continue;
        }
        // bounds: don't write past end of mapped image
        if ((SIZE_T)sec->VirtualAddress + sec->SizeOfRawData > (SIZE_T)imgSize)
        {
            Log("[-]", "    -> SKIP: VA+size exceeds image size");
            continue;
        }

        DWORD csz = sec->SizeOfRawData < sec->Misc.VirtualSize
                    ? sec->SizeOfRawData : sec->Misc.VirtualSize;
        memcpy(mapped.data() + sec->VirtualAddress,
               raw.data()    + sec->PointerToRawData, csz);
        Log("   ", "    -> mapped OK");
    }

    Log("[*]", "Writing image to target...");
    SIZE_T written = 0;
    if (!WriteProcessMemory(hProc, remBase, mapped.data(), imgSize, &written))
        { Log("[-]", "WPM image failed", GetLastError()); VirtualFreeEx(hProc, remBase, 0, MEM_RELEASE); return nullptr; }
    { char buf[64]; sprintf_s(buf, "Wrote %zu bytes", written); Log("[+]", buf); }

    // ---- Resolve RtlAddFunctionTable from ntdll ----------------------------
    HMODULE hNtdll  = GetModuleHandleA("ntdll.dll");
    HMODULE hK32    = GetModuleHandleA("kernel32.dll");

    // ---- Loader data -------------------------------------------------------
    Log("[*]", "Preparing loader data + shellcode...");
    LOADER_DATA ld = {};
    ld.fnLoadLibraryA      = (decltype(ld.fnLoadLibraryA))     GetProcAddress(hK32,   "LoadLibraryA");
    ld.fnGetProcAddress    = (decltype(ld.fnGetProcAddress))   GetProcAddress(hK32,   "GetProcAddress");
    ld.fnRtlAddFunctionTable = (decltype(ld.fnRtlAddFunctionTable)) GetProcAddress(hNtdll, "RtlAddFunctionTable");
    ld.pBase  = remBase;
    ld.delta  = delta;
    ld.step   = 0;
    ld.lastErr= 0;

    { char buf[128]; sprintf_s(buf, "RtlAddFunctionTable @ 0x%p", (void*)ld.fnRtlAddFunctionTable); Log("[*]", buf); }

    const SIZE_T SHELLCODE_REGION = 0x1000;
    const SIZE_T extraSz = sizeof(LOADER_DATA) + SHELLCODE_REGION;

    BYTE* remExtra = (BYTE*)VirtualAllocEx(hProc, nullptr, extraSz,
                                           MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    if (!remExtra) { Log("[-]", "VirtualAllocEx shellcode failed", GetLastError()); VirtualFreeEx(hProc, remBase, 0, MEM_RELEASE); return nullptr; }

    BYTE* remData = remExtra;
    BYTE* remCode = remExtra + sizeof(LOADER_DATA);

    WriteProcessMemory(hProc, remData, &ld,              sizeof(LOADER_DATA),   nullptr);
    WriteProcessMemory(hProc, remCode, (void*)LoaderShellcode, SHELLCODE_REGION, nullptr);
    { char buf[64]; sprintf_s(buf, "Shellcode @ 0x%llX", (ULONGLONG)remCode); Log("[+]", buf); }

    // ---- Stealth thread ----------------------------------------------------
    Log("[*]", "Spawning stealth thread (NtCreateThreadEx)...");
    HANDLE hThread = CreateStealtyhThread(hProc, remCode, remData);
    if (!hThread)
    {
        Log("[-]", "Thread creation failed", GetLastError());
        VirtualFreeEx(hProc, remBase,  0, MEM_RELEASE);
        VirtualFreeEx(hProc, remExtra, 0, MEM_RELEASE);
        return nullptr;
    }

    Log("[*]", "Waiting for loader (8s)...");
    DWORD wait = WaitForSingleObject(hThread, 8000);
    DWORD exitCode = 0;
    GetExitCodeThread(hThread, &exitCode);
    CloseHandle(hThread);

    // Read back debug state
    LOADER_DATA result = {};
    ReadProcessMemory(hProc, remData, &result, sizeof(LOADER_DATA), nullptr);
    VirtualFreeEx(hProc, remExtra, 0, MEM_RELEASE);

    (void)wait; (void)exitCode; // suppress unused-variable warnings

    if (result.step < 7)
    {
        char buf[64]; sprintf_s(buf, "Stalled at step %u", result.step);
        Log("[-]", buf);
        return nullptr;
    }

    Log("[+]", "Injection complete.");
    return remBase;
}

// ---------------------------------------------------------------------------
// Convert an RVA to a raw file offset using the section table
// ---------------------------------------------------------------------------
static DWORD RvaToOffset(IMAGE_NT_HEADERS* nt, DWORD rva)
{
    auto* sec = IMAGE_FIRST_SECTION(nt);
    for (WORD i = 0; i < nt->FileHeader.NumberOfSections; i++, sec++)
    {
        if (rva >= sec->VirtualAddress &&
            rva <  sec->VirtualAddress + sec->Misc.VirtualSize)
            return sec->PointerToRawData + (rva - sec->VirtualAddress);
    }
    return rva; // fallback: headers are identity-mapped
}

// ---------------------------------------------------------------------------
// Scan the export table of a raw PE (on-disk layout) to find a named export's RVA
// ---------------------------------------------------------------------------
static DWORD FindExportRVA(const std::vector<BYTE>& raw, const char* name)
{
    auto* dos = (IMAGE_DOS_HEADER*)raw.data();
    auto* nt  = (IMAGE_NT_HEADERS*)(raw.data() + dos->e_lfanew);
    auto& ed  = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT];
    if (!ed.Size) return 0;

    DWORD expOff = RvaToOffset(nt, ed.VirtualAddress);
    auto* exp  = (IMAGE_EXPORT_DIRECTORY*)(raw.data() + expOff);

    DWORD namesOff  = RvaToOffset(nt, exp->AddressOfNames);
    DWORD ordsOff   = RvaToOffset(nt, exp->AddressOfNameOrdinals);
    DWORD funcsOff  = RvaToOffset(nt, exp->AddressOfFunctions);

    auto* names = (DWORD*)(raw.data() + namesOff);
    auto* ords  = (WORD*) (raw.data() + ordsOff);
    auto* funcs = (DWORD*)(raw.data() + funcsOff);

    for (DWORD i = 0; i < exp->NumberOfNames; i++)
    {
        DWORD nameOff = RvaToOffset(nt, names[i]);
        const char* n = (const char*)(raw.data() + nameOff);
        if (strcmp(n, name) == 0)
            return funcs[ords[i]]; // this is already an RVA, return as-is
    }
    return 0;
}

// ---------------------------------------------------------------------------
// After injection, poll g_status in target memory and print what step we're at
// ---------------------------------------------------------------------------
static void PollStatus(HANDLE hProc, BYTE* remBase, const std::vector<BYTE>& raw)
{
    DWORD rva = FindExportRVA(raw, "g_status");
    if (!rva) { Log("[-]", "g_status export not found in DLL"); return; }

    BYTE* remStatus = remBase + rva;
    { char buf[80]; sprintf_s(buf, "g_status at remote 0x%llX - polling for 10s...", (ULONGLONG)remStatus); Log("[*]", buf); }

    auto DecodeStatus = [](DWORD v) -> const char* {
        switch (v) {
            case   0: return "DllMain not called yet";
            case   1: return "DllMain fired, thread spawning";
            case   2: return "MainThread running (post-sleep)";
            case   3: return "dummy window created";
            case   4: return "Present ptr grabbed from vtable";
            case   5: return "MH_Initialize OK";
            case   6: return "MH_CreateHook OK";
            case   7: return "HOOK INSTALLED - waiting for Present call";
            case 100: return "hkPresent fired (entering init)";
            case 101: return "GetDevice OK";
            case 102: return "GetImmediateContext + hwnd OK";
            case 103: return "GetBuffer OK (back buffer retrieved)";
            case 104: return "CreateRenderTargetView OK";
            case 105: return "SetWindowLongPtr done";
            case 106: return "ImGui init COMPLETE - overlay active";
            case 200: return "RenderOverlay executing - VISIBLE";
            case 201: return "RTV null recovery attempted";
            default:  return "unknown";
        }
    };

    DWORD lastVal = 0xFFFFFFFF;
    for (int tick = 0; tick < 40; tick++)   // poll 20s (40 x 500ms)
    {
        Sleep(500);
        DWORD val = 0;
        if (!ReadProcessMemory(hProc, remStatus, &val, sizeof(val), nullptr)) break;

        if (val != lastVal)
        {
            lastVal = val;
            char buf[128];
            sprintf_s(buf, "g_status = %u -> %s", val, DecodeStatus(val));
            bool good = (val == 106 || val == 200);
            Log(good ? "[+]" : "[*]", buf);
        }

        if (val == 200) break; // fully rendering
        if (val == 0 && tick > 4) break; // stalled at 0 after a few ticks
    }

    if (lastVal == 7)
        Log("[!]", "Stuck at 7 = hook installed but Present never called -> game may use DX12, not DX11");
    else if (lastVal == 100)
        Log("[!]", "Stuck at 100 = hkPresent fires but GetDevice FAILS (DX12 swapchain? wrong device interface)");
    else if (lastVal == 101)
        Log("[!]", "Stuck at 101 = GetDevice OK but GetImmediateContext/GetDesc failed");
    else if (lastVal == 103)
        Log("[!]", "Stuck at 103 = GetBuffer OK but CreateRenderTargetView failed");
    else if (lastVal == 104 || lastVal == 105)
        Log("[!]", "Stuck at 104/105 = RTV OK but ImGui init failed");
    else if (lastVal == 106)
        Log("[*]", "Init complete (106) - waiting for first RenderOverlay call...");
    else if (lastVal < 7 && lastVal > 0)
    { char buf[64]; sprintf_s(buf, "Stalled at %u - hook setup failed", lastVal); Log("[-]", buf); }
}

// ---------------------------------------------------------------------------
// Console helpers
// ---------------------------------------------------------------------------
static void SetupConsole()
{
    // ANSI escape codes
    HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD  mode = 0;
    GetConsoleMode(h, &mode);
    SetConsoleMode(h, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);

    // UTF-8 so any unicode survives; OEM codepage garbles box-drawing chars
    SetConsoleOutputCP(CP_UTF8);
}

// ANSI shortcuts
#define COL_RESET  "\033[0m"
#define COL_BOLD   "\033[1m"
#define COL_DIM    "\033[2m"
#define COL_WHITE  "\033[97m"
#define COL_GREEN  "\033[92m"
#define COL_RED    "\033[91m"
#define COL_YELLOW "\033[93m"
#define COL_CYAN   "\033[96m"
#define COL_GRAY   "\033[90m"

// UTF-8 box-drawing / glyphs (raw bytes so they survive regardless of
// source-file encoding; console is set to CP_UTF8 in SetupConsole)
#define BOX_H     "\xE2\x94\x80"   // ─
#define BOX_V     "\xE2\x94\x82"   // │
#define BOX_TL    "\xE2\x95\xAD"   // ╭
#define BOX_TR    "\xE2\x95\xAE"   // ╮
#define BOX_BL    "\xE2\x95\xB0"   // ╰
#define BOX_BR    "\xE2\x95\xAF"   // ╯
#define GLYPH_OK  "\xE2\x9C\x93"   // ✓
#define GLYPH_X   "\xE2\x9C\x97"   // ✗

static const int kBoxWidth = 28; // interior width, corner to corner

// Count UTF-8 codepoints (not bytes) so padding lines up even when the
// text contains a multi-byte glyph like GLYPH_OK / GLYPH_X.
static int Utf8Len(const char* s)
{
    int n = 0;
    for (; *s; s++)
        if ((*s & 0xC0) != 0x80) n++;
    return n;
}

static void BoxTop()
{
    printf("   " COL_CYAN BOX_TL);
    for (int i = 0; i < kBoxWidth; i++) printf(BOX_H);
    printf(BOX_TR COL_RESET "\n");
}

static void BoxBottom()
{
    printf("   " COL_CYAN BOX_BL);
    for (int i = 0; i < kBoxWidth; i++) printf(BOX_H);
    printf(BOX_BR COL_RESET "\n");
}

static void BoxLine(const char* color, const char* text)
{
    int pad = kBoxWidth - 2 - Utf8Len(text);
    if (pad < 0) pad = 0;
    int padL = pad / 2, padR = pad - padL;

    printf("   " COL_CYAN BOX_V COL_RESET " ");
    for (int i = 0; i < padL; i++) printf(" ");
    printf("%s%s" COL_RESET, color, text);
    for (int i = 0; i < padR; i++) printf(" ");
    printf(" " COL_CYAN BOX_V COL_RESET "\n");
}

// ---------------------------------------------------------------------------
// Entry - GUI window (no console)
// ---------------------------------------------------------------------------
#if 0
int main(int argc, char* argv[])
{
    SetupConsole();
    SetConsoleTitleA("paidini");

    // ---- Bootstrap folders + logger ----------------------------------------
    EnsureFolders();

    // ---- Resolve DLL path --------------------------------------------------
    std::string dllPathA;
    if (argc >= 2)
    {
        dllPathA = argv[1];
    }
    else
    {
        char exe[MAX_PATH];
        GetModuleFileNameA(nullptr, exe, MAX_PATH);
        std::string dir(exe);
        auto sl = dir.find_last_of("\\/");
        if (sl != std::string::npos) dir = dir.substr(0, sl + 1);
        dllPathA = dir + "paidini.dll";
    }
    std::wstring dllPathW(dllPathA.begin(), dllPathA.end());

    // ---- Log session header -------------------------------------------------
    {
        SYSTEMTIME st; GetLocalTime(&st);
        char hdr[128];
        sprintf_s(hdr, "==== session %04d-%02d-%02d %02d:%02d:%02d ====",
                  st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
        Log("[===]", hdr);
        Log("[===]", ("dll: " + dllPathA).c_str());
    }

    // ---- Header ------------------------------------------------------------
    printf("\n");
    BoxTop();
    BoxLine(COL_BOLD COL_WHITE, "PAIDINI");
    BoxLine(COL_GRAY, "manual map injector");
    BoxBottom();
    printf("\n");

    EnableDebugPrivilege();

    // ---- Wait for ARK ------------------------------------------------------
    std::vector<std::wstring> targets = {
        L"ArkAscended.exe",
        L"ShooterGame.exe",
        L"ARKSurvivalEvolved.exe"
    };

    static const char spinner[] = { '|', '/', '-', '\\' };

    DWORD pid = 0;
    for (int i = 0; i < 30 && !pid; i++)
    {
        printf("\r   " COL_DIM "%c waiting for ARK..." COL_RESET, spinner[i % 4]);
        fflush(stdout);
        pid = FindProcess(targets);
        if (!pid) Sleep(2000);
    }

    if (!pid)
    {
        printf("\r   " COL_RED GLYPH_X " ARK not found. launch the game first." COL_RESET "          \n\n");
        getchar();
        return 1;
    }

    printf("\r   " COL_GREEN GLYPH_OK " target found" COL_RESET "                    \n");

    HANDLE hProc = OpenProcess(PROCESS_ALL_ACCESS, FALSE, pid);
    if (!hProc)
    {
        printf("   " COL_RED GLYPH_X " OpenProcess failed. run as administrator." COL_RESET "\n\n");
        getchar();
        return 1;
    }

    GrantAppContainerAccess(hProc, dllPathW);

    // ---- Countdown ---------------------------------------------------------
    printf("   " COL_WHITE "injecting in" COL_RESET);
    for (int i = 3; i >= 1; i--)
    {
        printf(COL_YELLOW COL_BOLD "  %d" COL_RESET, i);
        fflush(stdout);
        Sleep(1000);
    }
    printf("\n\n");

    printf("   " COL_DIM "mapping sections..." COL_RESET);
    fflush(stdout);

    // ---- Inject ------------------------------------------------------------
    std::vector<BYTE> rawForExport;
    {
        HANDLE hEx = CreateFileA(dllPathA.c_str(), GENERIC_READ, FILE_SHARE_READ,
                                 nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (hEx != INVALID_HANDLE_VALUE)
        {
            LARGE_INTEGER liEx; GetFileSizeEx(hEx, &liEx);
            rawForExport.resize((size_t)liEx.QuadPart);
            DWORD rd = 0;
            ReadFile(hEx, rawForExport.data(), (DWORD)liEx.QuadPart, &rd, nullptr);
            CloseHandle(hEx);
        }
    }

    BYTE* remBase = ManualMap(hProc, dllPathA.c_str());

    if (remBase && !rawForExport.empty())
        PollStatus(hProc, remBase, rawForExport);

    CloseHandle(hProc);

    // Erase the "mapping sections..." status line
    printf("\r                                        \r");

    printf("\n");
    if (remBase)
    {
        Log("[+]", "injection complete - overlay active");

        char pidLine[48];
        sprintf_s(pidLine, "pid %lu", pid);

        BoxTop();
        BoxLine(COL_BOLD COL_GREEN, GLYPH_OK " INJECTED");
        BoxLine(COL_GRAY, pidLine);
        BoxBottom();
        printf("\n   " COL_DIM "press enter to close" COL_RESET);
        getchar();
    }
    else
    {
        Log("[-]", "injection failed");

        BoxTop();
        BoxLine(COL_BOLD COL_RED, GLYPH_X " FAILED");
        BoxLine(COL_GRAY, "check the log for details");
        BoxBottom();
        printf("\n   " COL_DIM "press enter to close" COL_RESET);
        getchar();
    }

    if (g_logHandle != INVALID_HANDLE_VALUE)
    {
        Log("[===]", "==== session end ====");
        CloseHandle(g_logHandle);
    }

    return remBase ? 0 : 1;
}
#endif // 0 - old console main disabled

#if 0 // GUI disabled - back to console-style like Admin Tool shot
// ---------------------------------------------------------------------------
// Separate GUI window - same injector, no cmd
// ---------------------------------------------------------------------------
static void GuiLinePlain(const char* s)
{
    if (!g_hLogList) return;
    // EDIT control - append line, no selection outline
    int len = GetWindowTextLengthA(g_hLogList);
    SendMessageA(g_hLogList, EM_SETSEL, (WPARAM)len, (LPARAM)len);
    std::string line = std::string(s) + "\r\n";
    SendMessageA(g_hLogList, EM_REPLACESEL, FALSE, (LPARAM)line.c_str());
}

static void GuiSetStatus(const char* s)
{
    strncpy_s(g_statusLine, s, sizeof(g_statusLine) - 1);
    GuiLinePlain(s);
    if (g_hMain)
        InvalidateRect(g_hMain, nullptr, FALSE);
}

static DWORD WINAPI InjectThreadProc(LPVOID)
{
    std::vector<std::wstring> targets = {
        L"ArkAscended.exe",
        L"ShooterGame.exe",
        L"ARKSurvivalEvolved.exe"
    };

    g_state = 0;

    DWORD pid = 0;
    for (int i = 0; i < 30 && !pid; i++)
    {
        pid = FindProcess(targets);
        if (!pid) Sleep(2000);
    }

    if (!pid)
    {
        GuiSetStatus("target not found - launch ARK first");
        g_state = 3;
        strcpy_s(g_resultLine, "not found");
        Log("[-]", "ARK not found");
        GuiSetStatus("press enter to close");
        while (!(GetAsyncKeyState(VK_RETURN) & 1)) Sleep(50);
        PostMessageA(g_hMain, WM_CLOSE, 0, 0);
        return 1;
    }

    {
        char b[64]; sprintf_s(b, "target found pid %lu", pid);
        Log("[+]", b);
    }
    {
        char b[64]; sprintf_s(b, "injecting pid %lu...", pid);
        GuiSetStatus(b);
    }

    HANDLE hProc = OpenProcess(PROCESS_ALL_ACCESS, FALSE, pid);
    if (!hProc)
    {
        GuiSetStatus("openprocess failed - run as admin");
        g_state = 3;
        strcpy_s(g_resultLine, "access denied");
        Log("[-]", "OpenProcess failed", GetLastError());
        GuiSetStatus("press enter to close");
        while (!(GetAsyncKeyState(VK_RETURN) & 1)) Sleep(50);
        PostMessageA(g_hMain, WM_CLOSE, 0, 0);
        return 1;
    }

    GrantAppContainerAccess(hProc, std::wstring(g_dllPathA.begin(), g_dllPathA.end()));

    g_state = 1;

    // show what we're loading - dll filename only
    {
        std::string name = g_dllPathA;
        auto sl = name.find_last_of("\\/");
        if (sl != std::string::npos) name = name.substr(sl + 1);
        char b[MAX_PATH + 32]; sprintf_s(b, "loading %s...", name.c_str());
        GuiSetStatus(b);
    }

    GuiSetStatus("mapping sections...");

    std::vector<BYTE> rawForExport;
    {
        HANDLE hEx = CreateFileA(g_dllPathA.c_str(), GENERIC_READ, FILE_SHARE_READ,
                                 nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (hEx != INVALID_HANDLE_VALUE)
        {
            LARGE_INTEGER liEx; GetFileSizeEx(hEx, &liEx);
            rawForExport.resize((size_t)liEx.QuadPart);
            DWORD rd = 0;
            ReadFile(hEx, rawForExport.data(), (DWORD)liEx.QuadPart, &rd, nullptr);
            CloseHandle(hEx);
        }
    }

    {
        std::string name = g_dllPathA;
        auto sl = name.find_last_of("\\/");
        if (sl != std::string::npos) name = name.substr(sl + 1);
        char b[MAX_PATH + 32]; sprintf_s(b, "loading modules: %s", name.c_str());
        GuiSetStatus(b);
    }

    BYTE* remBase = ManualMap(hProc, g_dllPathA.c_str());

    if (remBase && !rawForExport.empty())
    {
        GuiSetStatus("loading overlay...");
        PollStatus(hProc, remBase, rawForExport);
    }

    CloseHandle(hProc);

    if (remBase)
    {
        Log("[+]", "injection complete - overlay active");
        sprintf_s(g_resultLine, "pid %lu", pid);
        GuiSetStatus("injected");
        g_state = 2;
    }
    else
    {
        Log("[-]", "injection failed");
        strcpy_s(g_resultLine, "check debug.log");
        GuiSetStatus("injection failed - check debug.log");
        g_state = 3;
    }

    GuiSetStatus("press enter to close");
    if (g_hMain) InvalidateRect(g_hMain, nullptr, TRUE);
    while (!(GetAsyncKeyState(VK_RETURN) & 1)) Sleep(50);
    PostMessageA(g_hMain, WM_CLOSE, 0, 0);
    return remBase ? 0 : 1;
}

static LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg)
    {
    case WM_CREATE:
    {
        g_hBgBrush = CreateSolidBrush(RGB(12, 12, 12));

        g_hFont = CreateFontA(17, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, FF_MODERN, "Consolas");
        g_hFontBold = CreateFontA(17, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, FF_MODERN, "Consolas");

        // full-client terminal view - no border, no outline
        g_hStatus = nullptr;
        g_hCloseBtn = nullptr;

        RECT rc;
        GetClientRect(hWnd, &rc);
        g_hLogList = CreateWindowExA(0, "EDIT", nullptr,
            WS_CHILD | WS_VISIBLE | WS_VSCROLL |
            ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL | ES_NOHIDESEL,
            0, 0, rc.right - rc.left, rc.bottom - rc.top,
            hWnd, nullptr, nullptr, nullptr);
        SendMessage(g_hLogList, WM_SETFONT, (WPARAM)g_hFont, TRUE);

        // resolve dll path + logger (same as old console main)
        {
            char exe[MAX_PATH];
            GetModuleFileNameA(nullptr, exe, MAX_PATH);
            std::string dir(exe);
            auto sl = dir.find_last_of("\\/");
            if (sl != std::string::npos) dir = dir.substr(0, sl + 1);
            g_dllPathA = dir + "paidini.dll";
        }
        EnsureFolders();
        {
            SYSTEMTIME st; GetLocalTime(&st);
            char hdr[128];
            sprintf_s(hdr, "==== gui session %04d-%02d-%02d %02d:%02d:%02d ====",
                      st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
            Log("[===]", hdr);
            Log("[===]", ("dll: " + g_dllPathA).c_str());
        }
        EnableDebugPrivilege();

        CreateThread(nullptr, 0, InjectThreadProc, nullptr, 0, nullptr);
        return 0;
    }
    case WM_SIZE:
        if (g_hLogList)
            MoveWindow(g_hLogList, 0, 0, LOWORD(lp), HIWORD(lp), TRUE);
        return 0;
    case WM_CTLCOLORSTATIC:
    {
        HDC hdc = (HDC)wp;
        SetTextColor(hdc, RGB(204, 204, 204));
        SetBkColor(hdc, RGB(12, 12, 12));
        return (LRESULT)g_hBgBrush;
    }
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORLISTBOX:
    {
        HDC hdc = (HDC)wp;
        SetTextColor(hdc, RGB(204, 204, 204));
        SetBkColor(hdc, RGB(12, 12, 12));
        return (LRESULT)g_hBgBrush;
    }
    case WM_KEYDOWN:
        if (wp == VK_RETURN && g_state >= 2)
            DestroyWindow(hWnd);
        return 0;
    case WM_COMMAND:
        return 0;
    case WM_PAINT:
    {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hWnd, &ps);
        FillRect(hdc, &ps.rcPaint, g_hBgBrush);
        EndPaint(hWnd, &ps);
        return 0;
    }
    case WM_DESTROY:
        if (g_logHandle != INVALID_HANDLE_VALUE)
        {
            Log("[===]", "==== gui session end ====");
            CloseHandle(g_logHandle);
            g_logHandle = INVALID_HANDLE_VALUE;
        }
        if (g_hFont) DeleteObject(g_hFont);
        if (g_hFontBold) DeleteObject(g_hFontBold);
        if (g_hBgBrush) DeleteObject(g_hBgBrush);
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcA(hWnd, msg, wp, lp);
}

int WINAPI WinMain(HINSTANCE hInst, HINSTANCE, LPSTR, int nShow)
{
    const char* cls = "PaidiniInjector";
    WNDCLASSA wc = {};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInst;
    wc.lpszClassName = cls;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = CreateSolidBrush(RGB(12, 12, 12));
    RegisterClassA(&wc);

    g_hMain = CreateWindowExA(0, cls, "paidini",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, 1010, 620,
        nullptr, nullptr, hInst, nullptr);

    ShowWindow(g_hMain, nShow);
    UpdateWindow(g_hMain);

    MSG m;
    while (GetMessageA(&m, nullptr, 0, 0))
    {
        TranslateMessage(&m);
        DispatchMessageA(&m);
    }
    return 0;
}
#endif // GUI disabled

// ---------------------------------------------------------------------------
// Console window - looks like the Admin Tool shot but titled paidini
// Shows every module + bypass step, ends with press enter to close
// ---------------------------------------------------------------------------
static void CPrint(const char* s)
{
    printf("%s\n", s);
    fflush(stdout);
    Log("[ui]", s);
}

// single-enter close with 10s auto-close - far left, dim.
// Enter closes now, otherwise exits after autoCloseSec.
static void WaitEnterOnce(int autoCloseSec = 10)
{
    while (_kbhit()) _getch();
    for (int left = autoCloseSec; left >= 0; --left)
    {
        if (g_useColor)
            printf("\r\033[90mpress enter to close - auto closing in %d seconds  \033[0m", left);
        else
            printf("\rpress enter to close - auto closing in %d seconds  ", left);
        fflush(stdout);
        for (int i = 0; i < 10; ++i)
        {
            if (_kbhit())
            {
                int ch = _getch();
                if (ch == '\r' || ch == '\n') { printf("\n"); fflush(stdout); return; }
            }
            Sleep(100);
        }
    }
    printf("\n");
    fflush(stdout);
}

// visible length ignoring ANSI + counting UTF-8 codepoints as 1 col
static int VisibleLen(const char* s)
{
    int n = 0;
    for (const unsigned char* p = (const unsigned char*)s; *p; )
    {
        if (*p == 0x1B && *(p+1) == '[')
        {
            p += 2;
            while (*p && *p != 'm') p++;
            if (*p) p++;
            continue;
        }
        if ((*p & 0xC0) != 0x80) n++;
        p++;
    }
    return n;
}

static void PrintLeft(const char* ansiText)
{
    if (g_useColor)
    {
        printf("%s\n", ansiText);
    }
    else
    {
        // strip ESC[...m so old conhost shows clean text, no raw codes
        std::string s;
        for (const unsigned char* p = (const unsigned char*)ansiText; *p; )
        {
            if (*p == 0x1B && *(p+1) == '[')
            {
                p += 2;
                while (*p && *p != 'm') p++;
                if (*p) p++;
                continue;
            }
            s += (char)*p++;
        }
        printf("%s\n", s.c_str());
    }
    fflush(stdout);
}

// logo block centered as ONE block so edges never wobble.
// uses Ghost-proven box glyphs only, single white colour.
static void PrintLogoBlock()
{
    static const char* lines[] = {
        "██████╗  █████╗ ██╗██████╗ ██╗███╗   ██╗██╗",
        "██╔══██╗██╔══██╗██║██╔══██╗██║████╗  ██║██║",
        "██████╔╝███████║██║██║  ██║██║██╔██╗ ██║██║",
        "██╔═══╝ ██╔══██║██║██║  ██║██║██║╚██╗██║██║",
        "██║     ██║  ██║██║██████╔╝██║██║ ╚████║██║",
        "╚═╝     ╚═╝  ╚═╝╚═╝╚═════╝ ╚═╝╚═╝  ╚═══╝╚═╝",
    };
    int w = 80;
    CONSOLE_SCREEN_BUFFER_INFO csbi = {};
    if (GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &csbi) && csbi.dwSize.X > 0)
        w = csbi.dwSize.X;
    int maxVis = 0;
    for (auto L : lines)
    {
        int v = VisibleLen(L);
        if (v > maxVis) maxVis = v;
    }
    int pad = (w - maxVis) / 2; if (pad < 0) pad = 0;
    printf("\n");
    for (auto L : lines)
    {
        if (g_useColor) printf("%*s\033[97;1m%s\033[0m\n", pad, "", L);
        else printf("%*s%s\n", pad, "", L);
    }
    printf("\n");
    fflush(stdout);
}

int main(int argc, char* argv[])
{
    // always-show separate window like Ghost menu. --hidden = stealth, no window.
    bool hidden = false;
    for (int i = 1; i < argc; i++)
        if (!strcmp(argv[i], "--hidden") || !strcmp(argv[i], "-h"))
            hidden = true;

    if (hidden)
    {
        HWND cw = GetConsoleWindow();
        if (cw) ShowWindow(cw, SW_HIDE);
    }
    else
    {
        // Force classic conhost separate window like Ghost Inj.exe shot.
        // WT tabs all consoles into one window - conhost.exe bypasses that.
        bool spawned = false;
        for (int i = 1; i < argc; i++)
            if (!strcmp(argv[i], "--spawned")) spawned = true;
        if (!spawned)
        {
            wchar_t exeW[MAX_PATH] = {};
            GetModuleFileNameW(nullptr, exeW, MAX_PATH);
            wchar_t sysDir[MAX_PATH] = {};
            UINT sysLen = GetSystemDirectoryW(sysDir, MAX_PATH);
            std::wstring conhost = (sysLen > 0 && sysLen < MAX_PATH)
                ? (std::wstring(sysDir) + L"\\conhost.exe")
                : L"conhost.exe";
            // conhost hosts us in its own classic window, not a WT tab
            std::wstring cmd = L"\"" + conhost + L"\" \"" + exeW + L"\" --spawned";
            std::vector<wchar_t> cmdBuf(cmd.begin(), cmd.end());
            cmdBuf.push_back(0);
            STARTUPINFOW si = {};
            si.cb = sizeof(si);
            PROCESS_INFORMATION pi = {};
            if (CreateProcessW(nullptr, cmdBuf.data(), nullptr, nullptr, FALSE,
                               0, nullptr, nullptr, &si, &pi))
            {
                CloseHandle(pi.hThread);
                CloseHandle(pi.hProcess);
                return 0;
            }
            Log("[-]", "conhost respawn failed, staying in current console", GetLastError());
        }
        // Ghost setup_console: own size/pos + title, so it never looks docked
        HWND hw = GetConsoleWindow();
        if (hw) MoveWindow(hw, 300, 150, 700, 480, TRUE);
    }

    // separate console window, Ghost style (Win10 TH2+ VT, Win11 conhost/wt)
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD mode = 0;
    if (hOut && hOut != INVALID_HANDLE_VALUE &&
        GetConsoleMode(hOut, &mode) &&
        SetConsoleMode(hOut, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING))
    {
        g_useColor = true;
    }
    else
    {
        // Old Win10 conhost without VT: keep plain text so no raw ESC garbage.
        g_useColor = false;
        Log("[!]", "VT processing unavailable, colors disabled", GetLastError());
    }
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleTitleA("paidini");

    // Unicode-safe DLL path: exe dir + paidini.dll (wide), ANSI copy for display.
    g_dllPathW.clear();
    {
        std::string argDll;
        for (int i = 1; i < argc; i++)
        {
            if (argv[i][0] != '-') { argDll = argv[i]; break; }
        }
        if (!argDll.empty())
        {
            wchar_t full[MAX_PATH] = {};
            DWORD n = GetFullPathNameW(
                std::wstring(argDll.begin(), argDll.end()).c_str(), MAX_PATH, full, nullptr);
            if (n > 0 && n < MAX_PATH) g_dllPathW = full;
            else
            {
                int wlen = MultiByteToWideChar(CP_ACP, 0, argDll.c_str(), -1, nullptr, 0);
                if (wlen > 0)
                {
                    std::wstring w((size_t)wlen - 1, 0);
                    MultiByteToWideChar(CP_ACP, 0, argDll.c_str(), -1, &w[0], wlen);
                    g_dllPathW = w;
                }
            }
        }
        else
        {
            wchar_t exeW[MAX_PATH] = {};
            if (GetModuleFileNameW(nullptr, exeW, MAX_PATH))
            {
                std::wstring dir(exeW);
                auto sl = dir.find_last_of(L"\\/");
                if (sl != std::wstring::npos) dir = dir.substr(0, sl + 1);
                g_dllPathW = dir + L"paidini.dll";
            }
            else
            {
                Log("[-]", "GetModuleFileNameW failed", GetLastError());
                char exeA[MAX_PATH] = {};
                GetModuleFileNameA(nullptr, exeA, MAX_PATH);
                std::string dir(exeA);
                auto sl = dir.find_last_of("\\/");
                if (sl != std::string::npos) dir = dir.substr(0, sl + 1);
                std::string a = dir + "paidini.dll";
                g_dllPathW = std::wstring(a.begin(), a.end());
            }
        }
    }
    g_dllPathA = WideToUtf8(g_dllPathW);
    std::string dllPathA = g_dllPathA;

    EnsureFolders();
    {
        SYSTEMTIME st; GetLocalTime(&st);
        char hdr[128];
        sprintf_s(hdr, "==== console session %04d-%02d-%02d %02d:%02d:%02d ====",
                  st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
        Log("[===]", hdr);
        Log("[===]", ("dll: " + dllPathA).c_str());
    }
    EnableDebugPrivilege();

    if (!hidden)
    {
        PrintLogoBlock();
        Log("[ui]", "paidini");
    }

    // ARK: Survival Evolved (Windows Store) only
    std::vector<std::wstring> targets = {
        L"ShooterGame.exe"
    };
    DWORD pid = 0;
    for (int i = 0; i < 30 && !pid; i++)
    {
        pid = FindProcess(targets);
        if (!pid) Sleep(2000);
    }
    if (!pid)
    {
        Log("[-]", "target not found");
        if (!hidden) { PrintLeft("\033[91mARK not found\033[0m"); WaitEnterOnce(); }
        return 1;
    }
    Log("[+]", ("pid " + std::to_string(pid)).c_str());

    HANDLE hProc = OpenProcess(PROCESS_ALL_ACCESS, FALSE, pid);
    if (!hProc)
    {
        // Win11 hardened DACLs reject ALL_ACCESS: fall back to the exact
        // rights ManualMap needs instead of failing outright.
        DWORD need = PROCESS_CREATE_THREAD | PROCESS_QUERY_INFORMATION |
                     PROCESS_VM_OPERATION | PROCESS_VM_READ | PROCESS_VM_WRITE;
        hProc = OpenProcess(need, FALSE, pid);
        if (hProc) Log("[+]", "OpenProcess fell back to minimal rights");
    }
    if (!hProc)
    {
        Log("[-]", "OpenProcess failed", GetLastError());
        if (!hidden) { PrintLeft("\033[91mrun as admin\033[0m"); WaitEnterOnce(); }
        return 1;
    }

    GrantAppContainerAccess(hProc, g_dllPathW);

    // countdown synced so dll lands on 1
    if (!hidden)
    {
        for (int i = 3; i >= 1; i--)
        {
            char buf[64]; sprintf_s(buf, "\033[93m● injecting %d...\033[0m", i);
            PrintLeft(buf);
            Sleep(600);
        }
    }

    std::vector<BYTE> rawForExport;
    {
        HANDLE hEx = CreateFileW(g_dllPathW.c_str(), GENERIC_READ, FILE_SHARE_READ,
                                 nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (hEx == INVALID_HANDLE_VALUE)
        {
            Log("[-]", "Cannot open DLL for export scan", GetLastError());
        }
        else
        {
            LARGE_INTEGER liEx; GetFileSizeEx(hEx, &liEx);
            rawForExport.resize((size_t)liEx.QuadPart);
            DWORD rd = 0;
            ReadFile(hEx, rawForExport.data(), (DWORD)liEx.QuadPart, &rd, nullptr);
            CloseHandle(hEx);
        }
    }

    BYTE* remBase = ManualMap(hProc, g_dllPathW);
    if (remBase && !rawForExport.empty())
        PollStatus(hProc, remBase, rawForExport);
    CloseHandle(hProc);

    if (!hidden)
    {
        if (remBase) PrintLeft("\033[92;1m✔ injected\033[0m");
        else PrintLeft("\033[91;1m✘ failed - check debug.log\033[0m");
        WaitEnterOnce();
    }

    if (g_logHandle != INVALID_HANDLE_VALUE)
    {
        Log("[===]", "==== console session end ====");
        CloseHandle(g_logHandle);
    }
    return remBase ? 0 : 1;
}
