// Minimal ARK Store/UWP host for TurretEditor. This intentionally does not
// include the Past Owl menu or any of the other paidini feature modules.
#include <Windows.h>
#include <windowsx.h>
#include <d3d11.h>
#include <dxgi.h>
#include <atomic>
#include <cfloat>
#include <mutex>
#include <algorithm>

#include "MinHook.h"
#include "imgui.h"
#include "backends/imgui_impl_win32.h"
#include "backends/imgui_impl_dx11.h"
#include "Global.h"
#include "Client Manager/Modules/Engine/EngineRefs.h"
#include "Client Manager/Modules/Misc/TurretEditor.h"
#include "Client Manager/Modules/UI/Input Bridge/InputBridge.h"

extern IMGUI_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND, UINT, WPARAM, LPARAM);

namespace TurretHost
{
    using PresentFn = HRESULT(__stdcall*)(IDXGISwapChain*, UINT, UINT);
    using ProcessEventFn = void(*)(void*, UFunction*, void*);
    using DrawHudFn = void(__fastcall*)(AHUD*);

    inline HMODULE module = nullptr;
    inline HWND gameWindow = nullptr;
    inline IDXGISwapChain* swapChain = nullptr;
    inline ID3D11Device* device = nullptr;
    inline ID3D11DeviceContext* context = nullptr;
    inline ID3D11RenderTargetView* target = nullptr;
    inline PresentFn originalPresent = nullptr;
    inline DrawHudFn originalDrawHud = nullptr;
    inline WNDPROC originalWndProc = nullptr;
    inline std::atomic<bool> stopping{ false };
    inline std::atomic<bool> menuOpen{ false };
    inline std::atomic<LONG64> pointerPosition{ 0 };
    inline std::atomic<LONG> wheelDelta{ 0 };
    inline std::atomic<LONG> pointerValid{ 0 };
    inline bool initialized = false;
    inline volatile LONG processEventCalls = 0;
    inline volatile LONG lastLearnState = -1;
    inline volatile LONG lastEditorVisibility = -1;
    inline volatile LONG pointerUpdateLogged = 0;
    inline volatile LONG mouseMoveLogged = 0;
    inline bool cursorSaved = false;
    inline bool oldCursor = false;
    inline bool showCountSaved = false;
    inline int oldShowCount = -1;
    inline APlayerController* blockedController = nullptr;
    inline MemoryManager memory;

    inline HWND FindGameWindow()
    {
        HWND frame = FindWindowA("ApplicationFrameWindow", "ARK: Survival Evolved");
        if (frame)
        {
            HWND core = FindWindowExA(frame, nullptr, "Windows.UI.Core.CoreWindow", "ARK: Survival Evolved");
            if (core) return core;
        }
        HWND h = nullptr;
        do
        {
            h = FindWindowExA(nullptr, h, "Windows.UI.Core.CoreWindow", nullptr);
            if (h)
            {
                DWORD pid = 0;
                GetWindowThreadProcessId(h, &pid);
                if (pid == GetCurrentProcessId()) return h;
            }
        } while (h);
        return nullptr;
    }

    inline LRESULT WINAPI WindowProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
    {
        constexpr UINT WM_POINTERUPDATE_ = 0x0245;
        constexpr UINT WM_POINTERDOWN_ = 0x0246;
        constexpr UINT WM_POINTERUP_ = 0x0247;
        constexpr UINT WM_POINTERWHEEL_ = 0x024E;
        if (menuOpen.load())
        {
            if (msg == WM_SETCURSOR)
            {
                SetCursor(LoadCursorW(nullptr, MAKEINTRESOURCEW(32512)));
                return TRUE;
            }
            if (msg == WM_POINTERUPDATE_ || msg == WM_POINTERDOWN_ || msg == WM_POINTERUP_)
            {
                POINT p{ GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
                POINTER_INFO pi{};
                const UINT32 pointerId = GET_POINTERID_WPARAM(wp);
                if (GetPointerInfo(pointerId, &pi)) p = pi.ptPixelLocation;
                if (ScreenToClient(hwnd, &p))
                {
                    RECT r{};
                    GetClientRect(hwnd, &r);
                    pointerPosition.store((LONG64)(uint32_t)p.x | ((LONG64)(uint32_t)p.y << 32));
                    pointerValid.store(r.right > 0 && r.bottom > 0 ? 1 : 0);
                    if (InterlockedCompareExchange(&pointerUpdateLogged, 1, 0) == 0)
                        Logger::Log("[TurretOnly] pointer messages received; client position=%ld,%ld window=%ldx%ld",
                                    p.x, p.y, r.right, r.bottom);
                }
            }
            else if (msg == WM_MOUSEMOVE)
            {
                const LONG x = GET_X_LPARAM(lp), y = GET_Y_LPARAM(lp);
                RECT r{};
                GetClientRect(hwnd, &r);
                pointerPosition.store((LONG64)(uint32_t)x | ((LONG64)(uint32_t)y << 32));
                pointerValid.store(r.right > 0 && r.bottom > 0 ? 1 : 0);
                if (InterlockedCompareExchange(&mouseMoveLogged, 1, 0) == 0)
                    Logger::Log("[TurretOnly] mouse-move messages received; client position=%ld,%ld window=%ldx%ld",
                                x, y, r.right, r.bottom);
            }
            if (msg == WM_MOUSEWHEEL || msg == WM_POINTERWHEEL_)
                wheelDelta.fetch_add((short)HIWORD(wp));

            if ((msg >= WM_KEYFIRST && msg <= WM_KEYLAST) ||
                (msg >= WM_MOUSEFIRST && msg <= WM_MOUSELAST) ||
                (msg >= 0x0240 && msg <= 0x0257) || msg == WM_INPUT)
                return 0;
        }
        if (originalWndProc) return CallWindowProcW(originalWndProc, hwnd, msg, wp, lp);
        return DefWindowProcW(hwnd, msg, wp, lp);
    }

    inline int QueryShowCursorCount()
    {
        // ShowCursor has no read-only query. This balanced pair returns the
        // current count without changing it overall.
        ShowCursor(FALSE);
        return ShowCursor(TRUE);
    }

    inline void ForceShowCursorCount(int target)
    {
        int count = QueryShowCursorCount();
        while (count < target) count = ShowCursor(TRUE);
        while (count > target) count = ShowCursor(FALSE);
    }

    inline void EnsureEditorCursor()
    {
        if (!showCountSaved)
        {
            oldShowCount = QueryShowCursorCount();
            showCountSaved = true;
        }
        if (QueryShowCursorCount() < 0) ShowCursor(TRUE);
        ClipCursor(nullptr); // UE otherwise recaptures/clips after a click.
    }

    inline void ApplyGameInputState()
    {
        if (!EngineRefs::Ready()) return;
        APlayerController* pc = ULocalPlayer::Get() ? ULocalPlayer::Get()->PlayerController : nullptr;
        if (blockedController && blockedController != pc)
        {
            blockedController = nullptr;
            cursorSaved = false;
        }
        const bool want = menuOpen.load();
        if (!pc)
        {
            static volatile LONG missingPcLogged = 0;
            if (want && InterlockedCompareExchange(&missingPcLogged, 1, 0) == 0)
                Logger::Log("[TurretOnly] editor is visible but no PlayerController is available for input gating");
            return;
        }
        if (want && blockedController != pc)
        {
            oldCursor = pc->bShowMouseCursor;
            cursorSaved = true;
            pc->bShowMouseCursor = true;
            EnsureEditorCursor();
            const auto& refs = EngineRefs::g_refs;
            if (refs.actorDisableInput)
            {
                struct { APlayerController* PlayerController; } p{ pc };
                UObject::ProcessEvent(pc, refs.actorDisableInput, &p);
                if (refs.pcGetPlayerCharacter)
                {
                    struct { AActor* ReturnValue; } out{};
                    UObject::ProcessEvent(pc, refs.pcGetPlayerCharacter, &out);
                    if (out.ReturnValue) UObject::ProcessEvent(out.ReturnValue, refs.actorDisableInput, &p);
                }
            }
            blockedController = pc;
            Logger::Log("[TurretOnly] game controls blocked; editor input gate OPEN");
        }
        else if (want)
        {
            // ARK can recapture and hide the cursor after a mouse click.
            pc->bShowMouseCursor = true;
            EnsureEditorCursor();
        }
        else if (!want && blockedController == pc)
        {
            if (cursorSaved) pc->bShowMouseCursor = oldCursor;
            if (showCountSaved) ForceShowCursorCount(oldShowCount);
            showCountSaved = false;
            const auto& refs = EngineRefs::g_refs;
            if (refs.actorEnableInput)
            {
                struct { APlayerController* PlayerController; } p{ pc };
                UObject::ProcessEvent(pc, refs.actorEnableInput, &p);
                if (refs.pcGetPlayerCharacter)
                {
                    struct { AActor* ReturnValue; } out{};
                    UObject::ProcessEvent(pc, refs.pcGetPlayerCharacter, &out);
                    if (out.ReturnValue) UObject::ProcessEvent(out.ReturnValue, refs.actorEnableInput, &p);
                }
            }
            blockedController = nullptr;
            cursorSaved = false;
            Logger::Log("[TurretOnly] game controls restored; editor input gate CLOSED");
        }
    }

    inline bool InitImGui(IDXGISwapChain* sc)
    {
        if (!gameWindow) gameWindow = FindGameWindow();
        if (!gameWindow || !IsWindow(gameWindow)) return false;
        if (FAILED(sc->GetDevice(__uuidof(ID3D11Device), (void**)&device))) return false;
        device->GetImmediateContext(&context);
        swapChain = sc;
        DXGI_SWAP_CHAIN_DESC desc{};
        sc->GetDesc(&desc);
        ID3D11Texture2D* backBuffer = nullptr;
        if (FAILED(sc->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&backBuffer))) return false;
        device->CreateRenderTargetView(backBuffer, nullptr, &target);
        backBuffer->Release();
        if (!target) return false;

        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO();
        io.IniFilename = nullptr;
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
        io.MouseDrawCursor = false;
        ImGui::StyleColorsDark();
        ImGui_ImplWin32_Init(gameWindow);
        ImGui_ImplDX11_Init(device, context);
        SetLastError(0);
        originalWndProc = reinterpret_cast<WNDPROC>(SetWindowLongPtrW(gameWindow, GWLP_WNDPROC, (LONG_PTR)&WindowProc));
        const DWORD wndProcError = GetLastError();
        char windowClass[128] = {};
        GetClassNameA(gameWindow, windowClass, ARRAYSIZE(windowClass));
        initialized = true;
        Logger::Log("[TurretOnly] ImGui initialized on %p class=%s (%lu x %lu), wndproc original=%p error=%lu",
                    gameWindow, windowClass, desc.BufferDesc.Width, desc.BufferDesc.Height,
                    originalWndProc, wndProcError);
        return true;
    }

    inline void CreateTarget(IDXGISwapChain* sc)
    {
        if (target) { target->Release(); target = nullptr; }
        ID3D11Texture2D* backBuffer = nullptr;
        if (SUCCEEDED(sc->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&backBuffer)) && backBuffer)
        {
            device->CreateRenderTargetView(backBuffer, nullptr, &target);
            backBuffer->Release();
        }
    }

    inline void UpdatePointerAndButtons()
    {
        ImGuiIO& io = ImGui::GetIO();
        const bool editorOpen = menuOpen.load();
        io.MouseDrawCursor = false; // use the real Windows cursor, shown above
        RECT rect{};
        if (gameWindow && GetClientRect(gameWindow, &rect))
        {
            io.DisplaySize = ImVec2((float)rect.right, (float)rect.bottom);
            if (editorOpen)
            {
                POINT p{};
                if (GetCursorPos(&p) && ScreenToClient(gameWindow, &p))
                {
                    pointerPosition.store((LONG64)(uint32_t)p.x | ((LONG64)(uint32_t)p.y << 32));
                    pointerValid.store(1);
                    io.AddMousePosEvent((float)p.x, (float)p.y);
                }
                else if (pointerValid.load())
                {
                    const LONG64 p0 = pointerPosition.load();
                    io.AddMousePosEvent((float)(uint32_t)p0, (float)(uint32_t)((uint64_t)p0 >> 32));
                }
                else io.AddMousePosEvent(-FLT_MAX, -FLT_MAX);
            }
            else io.AddMousePosEvent(-FLT_MAX, -FLT_MAX);
        }
        static const int buttons[5] = { VK_LBUTTON, VK_RBUTTON, VK_MBUTTON, VK_XBUTTON1, VK_XBUTTON2 };
        for (int i = 0; i < 5; ++i) io.AddMouseButtonEvent(i, editorOpen && InputBridge::g_down[buttons[i]]);
        const LONG wheel = wheelDelta.exchange(0);
        if (editorOpen && wheel) io.AddMouseWheelEvent(0.f, (float)wheel / WHEEL_DELTA);
        io.AddKeyEvent(ImGuiKey_F7, InputBridge::g_down[VK_F7]);
        TurretEditor::SetF7Down(InputBridge::g_down[VK_F7]);
        if (!editorOpen) pointerValid.store(0);
    }

    inline HRESULT __stdcall Present(IDXGISwapChain* sc, UINT sync, UINT flags)
    {
        if (stopping.load()) return originalPresent(sc, sync, flags);
        if (!initialized)
        {
            if (!InitImGui(sc)) return originalPresent(sc, sync, flags);
        }
        CreateTarget(sc);
        if (target)
        {
            ImGui::GetIO().MouseDrawCursor = false;
            ImGui_ImplDX11_NewFrame();
            ImGui_ImplWin32_NewFrame();
            UpdatePointerAndButtons();
            ImGui::NewFrame();
            TurretEditor::DrawUI();
            ImGui::Render();
            ID3D11RenderTargetView* oldTarget = nullptr;
            context->OMGetRenderTargets(1, &oldTarget, nullptr);
            context->OMSetRenderTargets(1, &target, nullptr);
            ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
            context->OMSetRenderTargets(1, &oldTarget, nullptr);
            if (oldTarget) oldTarget->Release();
            target->Release(); target = nullptr;
        }
        return originalPresent(sc, sync, flags);
    }

    inline void __fastcall DrawHud(AHUD* hud)
    {
        if (originalDrawHud) originalDrawHud(hud);
        if (stopping.load()) return;
        __try { TurretEditor::Tick(true, hud); }
        __except (EXCEPTION_EXECUTE_HANDLER) { Logger::Log("[TurretOnly] turret tick exception 0x%08X", GetExceptionCode()); }
        menuOpen.store(Settings::Turret::Visible);
        const LONG visibleNow = Settings::Turret::Visible ? 1 : 0;
        const LONG visibleWas = InterlockedExchange(&lastEditorVisibility, visibleNow);
        if (visibleWas != visibleNow)
            Logger::Log("[TurretOnly] editor visibility changed: %s", visibleNow ? "OPEN" : "CLOSED");
        __try { ApplyGameInputState(); }
        __except (EXCEPTION_EXECUTE_HANDLER) { Logger::Log("[TurretOnly] input state exception 0x%08X", GetExceptionCode()); }
    }

    inline void LogProcessEventDiagnostic(LONG callNo, void* object, UFunction* fn, LONG learnNow)
    {
        if (callNo <= 12 && fn)
        {
            const std::string eventName = fn->GetName();
            const std::string objectClass = TurretEditor::ClassNameOf(reinterpret_cast<UObject*>(object));
            Logger::Log("[TurretOnly] ProcessEvent detour call %ld: %s on %s; learn=%ld",
                        callNo, eventName.c_str(), objectClass.c_str(), learnNow);
        }
    }

    inline void ProcessEvent(void* object, UFunction* fn, void* params)
    {
        const LONG callNo = InterlockedIncrement(&processEventCalls);
        const LONG learnNow = Settings::Turret::Learn ? 1 : 0;
        const LONG learnWas = InterlockedExchange(&lastLearnState, learnNow);
        if (learnWas != learnNow)
            Logger::Log("[TurretOnly] command learning is %s", learnNow ? "ON" : "OFF");
        LogProcessEventDiagnostic(callNo, object, fn, learnNow);
        // Always observe the multi-use call chain for diagnostics. The observer
        // itself learns commands only while Learn is enabled.
        if (!stopping.load())
        {
            __try { TurretEditor::OnPreEvent(object, fn, params); }
            __except (EXCEPTION_EXECUTE_HANDLER) { Logger::Log("[TurretOnly] ProcessEvent observer exception 0x%08X", GetExceptionCode()); }
        }
        UObject::oProcessEvent(object, fn, params);
        if (!stopping.load())
        {
            __try { TurretEditor::OnPostEvent(object, fn, params); }
            __except (EXCEPTION_EXECUTE_HANDLER) { Logger::Log("[TurretOnly] ProcessEvent post-observer exception 0x%08X", GetExceptionCode()); }
        }
    }

    inline void InputThread()
    {
        HWND wnd = nullptr;
        for (int i = 0; i < 1200 && !wnd && !stopping.load(); ++i) { wnd = FindGameWindow(); if (!wnd) Sleep(100); }
        if (!wnd) return;
        gameWindow = wnd;
        const DWORD myTid = GetCurrentThreadId();
        const DWORD gameTid = GetWindowThreadProcessId(wnd, nullptr);
        if (gameTid && gameTid != myTid) AttachThreadInput(myTid, gameTid, TRUE);
        while (!stopping.load())
        {
            if (!IsWindow(gameWindow)) gameWindow = FindGameWindow();
            InputBridge::Poll(true);
            Sleep(8);
        }
    }

    inline void* GetPresentAddress()
    {
        WNDCLASSEXA wc{ sizeof(wc) };
        wc.lpfnWndProc = DefWindowProcA;
        wc.hInstance = GetModuleHandleA(nullptr);
        wc.lpszClassName = "TurretOnlyDummy";
        RegisterClassExA(&wc);
        HWND hwnd = CreateWindowExA(0, wc.lpszClassName, "d", WS_OVERLAPPEDWINDOW, 0, 0, 8, 8, nullptr, nullptr, wc.hInstance, nullptr);
        if (!hwnd) { UnregisterClassA(wc.lpszClassName, wc.hInstance); return nullptr; }
        DXGI_SWAP_CHAIN_DESC desc{};
        desc.BufferCount = 1; desc.BufferDesc.Width = 8; desc.BufferDesc.Height = 8;
        desc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT; desc.OutputWindow = hwnd;
        desc.SampleDesc.Count = 1; desc.Windowed = TRUE; desc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
        const D3D_FEATURE_LEVEL levels[] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0 };
        IDXGISwapChain* sc = nullptr; ID3D11Device* dev = nullptr; ID3D11DeviceContext* ctx = nullptr; D3D_FEATURE_LEVEL got{};
        const HRESULT hr = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, levels, ARRAYSIZE(levels), D3D11_SDK_VERSION, &desc, &sc, &dev, &got, &ctx);
        void* result = SUCCEEDED(hr) && sc ? (*reinterpret_cast<void***>(sc))[8] : nullptr;
        if (sc) sc->Release(); if (dev) dev->Release(); if (ctx) ctx->Release();
        DestroyWindow(hwnd); UnregisterClassA(wc.lpszClassName, wc.hInstance);
        return result;
    }

    inline DWORD WINAPI MainThread(void*)
    {
        Logger::Initialize();
        Logger::Log("[TurretOnly] starting isolated turret editor host");
        if (!InitSDK()) { Logger::Log("[TurretOnly] SDK init failed"); return 1; }
        EngineRefs::Start();
        CreateThread(nullptr, 0, [](LPVOID) -> DWORD { InputThread(); return 0; }, nullptr, 0, nullptr);
        if (MH_Initialize() != MH_OK) { Logger::Log("[TurretOnly] MinHook init failed"); return 2; }
        void* present = GetPresentAddress();
        if (!present || MH_CreateHook(present, &Present, (void**)&originalPresent) != MH_OK)
        { Logger::Log("[TurretOnly] Present hook setup failed"); return 3; }
        if (UObject::pProcessEvent && MH_CreateHook((void*)UObject::pProcessEvent, &ProcessEvent, (void**)&UObject::oProcessEvent) == MH_OK)
            Logger::Log("[TurretOnly] ProcessEvent hook ready");
        else Logger::Log("[TurretOnly] ProcessEvent hook failed; command learning unavailable");
        auto hits = memory.PatternScan("48 8B C4 55 41 54 41 55 41 56 41 57 48 8D A8 48 F9");
        if (!hits.empty() && MH_CreateHook(hits[0], &DrawHud, (void**)&originalDrawHud) == MH_OK)
            Logger::Log("[TurretOnly] DrawHUD hook ready");
        else Logger::Log("[TurretOnly] DrawHUD hook failed; game-side apply unavailable");
        if (MH_EnableHook(MH_ALL_HOOKS) != MH_OK) { Logger::Log("[TurretOnly] enabling hooks failed"); return 4; }
        Logger::Log("[TurretOnly] ready; press F7 to open editor");
        return 0;
    }
}

BOOL APIENTRY DllMain(HMODULE h, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH)
    {
        TurretHost::module = h;
        DisableThreadLibraryCalls(h);
        HANDLE thread = CreateThread(nullptr, 0, TurretHost::MainThread, nullptr, 0, nullptr);
        if (thread) CloseHandle(thread);
    }
    return TRUE;
}
