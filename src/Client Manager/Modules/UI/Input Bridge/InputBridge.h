#pragma once
#include <Windows.h>
#include <vector>
#include <mutex>

// ---------------------------------------------------------------------------
// InputBridge - collects keyboard input on the InputThread (the only thread
// AttachThreadInput'd to ARK's UI thread, so GetAsyncKeyState returns real
// state) and hands it to the Present thread via UpdateImGuiInput().
// Mirrors the working (arkholes) base: ToUnicodeEx gives layout/shift/caps
// correct characters, so text fields work on any keyboard layout.
// ---------------------------------------------------------------------------

namespace InputBridge
{
    struct KeyEvt { int vk; bool down; };

    inline std::mutex                g_mtx;
    inline std::vector<unsigned int> g_chars;
    inline std::vector<unsigned int> g_pastePending;
    inline std::vector<KeyEvt>       g_keys;
    inline bool g_ctrl = false, g_shift = false, g_alt = false;

    inline volatile bool g_capturing = false;
    inline volatile int  g_captureKey = 0;

    inline volatile bool g_down[256] = {};

    inline void Poll(bool menuOpen)
    {
        static bool prev[256] = {};

        BYTE kb[256] = {};
        if (GetAsyncKeyState(VK_SHIFT) & 0x8000) { kb[VK_SHIFT] = 0x80; kb[VK_LSHIFT] = 0x80; kb[VK_RSHIFT] = 0x80; }
        if (GetKeyState(VK_CAPITAL) & 0x0001)      kb[VK_CAPITAL] = 0x01;
        const HKL layout = GetKeyboardLayout(0);
        const bool ctrl  = (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0;

        std::lock_guard<std::mutex> lk(g_mtx);
        g_ctrl  = ctrl;
        g_shift = (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;
        g_alt   = (GetAsyncKeyState(VK_MENU)  & 0x8000) != 0;

        // Ctrl+V paste
        {
            static bool prevV = false;
            const bool vDown = (GetAsyncKeyState('V') & 0x8000) != 0;
            if (menuOpen && ctrl && vDown && !prevV)
            {
                g_pastePending.clear();
                if (OpenClipboard(nullptr))
                {
                    if (HANDLE h = GetClipboardData(CF_UNICODETEXT))
                    {
                        if (WCHAR* t = reinterpret_cast<WCHAR*>(GlobalLock(h)))
                        {
                            for (int i = 0; t[i] && i < 200000; i++)
                                if (t[i] >= 0x20 || t[i] == L'\t') g_pastePending.push_back((unsigned int)t[i]);
                            GlobalUnlock(h);
                        }
                    }
                    else if (HANDLE h2 = GetClipboardData(CF_TEXT))
                    {
                        if (char* a = reinterpret_cast<char*>(GlobalLock(h2)))
                        {
                            for (int i = 0; a[i] && i < 200000; i++)
                            {
                                unsigned char c = (unsigned char)a[i];
                                if (c >= 0x20 || c == '\t') g_pastePending.push_back((unsigned int)c);
                            }
                            GlobalUnlock(h2);
                        }
                    }
                    CloseClipboard();
                }
            }
            prevV = vDown;

            if (!ctrl && !g_pastePending.empty())
            {
                g_chars.insert(g_chars.end(), g_pastePending.begin(), g_pastePending.end());
                g_pastePending.clear();
            }
        }

        {
            static const int kMouseVk[5] = { VK_LBUTTON, VK_RBUTTON, VK_MBUTTON, VK_XBUTTON1, VK_XBUTTON2 };
            for (int mv : kMouseVk)
                g_down[mv] = (GetAsyncKeyState(mv) & 0x8000) != 0;
        }

        for (int vk = 0x08; vk <= 0xFE; vk++)
        {
            const bool down = (GetAsyncKeyState(vk) & 0x8000) != 0;
            const bool was  = prev[vk];
            prev[vk] = down;
            g_down[vk] = down;
            if (down == was) continue;

            if (!menuOpen) continue;

            if (down && g_capturing) { g_captureKey = vk; g_capturing = false; }

            // All transitions; UpdateImGuiInput maps the ones ImGui knows.
            g_keys.push_back({ vk, down });

            if (down)
            {
                if (vk == VK_SHIFT || vk == VK_LSHIFT || vk == VK_RSHIFT ||
                    vk == VK_CONTROL || vk == VK_MENU || vk == VK_CAPITAL || vk == VK_ESCAPE)
                    continue;
                if (ctrl) continue;

                WCHAR buf[4] = {};
                const UINT sc = MapVirtualKeyW(vk, MAPVK_VK_TO_VSC);
                const int  n  = ToUnicodeEx(vk, sc, kb, buf, 4, 0, layout);
                for (int i = 0; i < n; i++)
                    if (buf[i] >= 0x20)
                        g_chars.push_back(buf[i]);
            }
        }
    }
}
