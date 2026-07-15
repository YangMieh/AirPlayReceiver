// AirPlayServer.cpp : AirPlay Receiver - Main Program
// Starts automatically and waits for AirPlay connections.
// Window shows home screen with ImGui UI.
//
#include <windows.h>
#include <signal.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <imm.h>
#pragma comment(lib, "imm32.lib")
#include "Airplay2Head.h"
#include "CAirServerCallback.h"
#include "SDL.h"
#include "CSDLPlayer.h"

// Global player pointer for cleanup handlers
static CSDLPlayer* g_pPlayer = NULL;
static volatile bool g_bShuttingDown = false;

// Cleanup function to stop server gracefully
void CleanupAndShutdown()
{
    if (g_bShuttingDown) {
        return;  // Prevent re-entrancy
    }
    g_bShuttingDown = true;

    if (g_pPlayer != NULL) {
        g_pPlayer->m_server.stop();
        Sleep(150);
    }
}

// atexit handler as a fallback
void AtExitHandler()
{
    CleanupAndShutdown();
}

// --- 60fps + supersampling hook ------------------------------------------
// The DLL advertises its display capability to the iPhone in a plaintext /info
// bplist sent over ws2_32. We intercept send() and rewrite two things in that
// bplist so the iPhone actually SENDS a better mirror stream:
//   * maxFPS / refreshRate  30 -> 60   (bplist int 0x10 0x1E -> 0x10 0x3C)
//   * display size          3440x1440 -> 3840x2880  (supersampling)
// This is the only piece of our fork that upstream (xenos) does not do: their
// 60fps presets only pace the render side, they never change what the phone sends.
typedef int (WINAPI *send_t)(SOCKET, const char*, int, int);
static send_t g_realSend = NULL;
static int WINAPI MySend(SOCKET s, const char* buf, int len, int flags)
{
    if (buf && len > 40 && len < 8192) {
        bool hasBp = false, hasFps = false;
        for (int i = 0; i + 8 <= len; i++) { if (memcmp(buf + i, "bplist00", 8) == 0) { hasBp = true; break; } }
        if (hasBp) for (int i = 0; i + 6 <= len; i++) { if (memcmp(buf + i, "maxFPS", 6) == 0) { hasFps = true; break; } }
        if (hasBp && hasFps) {
            static char mbuf[8192];
            memcpy(mbuf, buf, len);
            int fps = 0, dim = 0;
            for (int i = 0; i + 2 < len; i++) {
                BYTE a = mbuf[i], b = mbuf[i + 1], c = mbuf[i + 2];
                if (a == 0x10 && b == 0x1E) { mbuf[i + 1] = 0x3C; fps++; i++; }               // maxFPS/refreshRate 30->60
                else if (a == 0x11 && b == 0x05 && c == 0xA0) { mbuf[i + 1] = 0x0B; mbuf[i + 2] = 0x40; dim++; i += 2; }  // height 1440->2880
                else if (a == 0x11 && b == 0x0D && c == 0x70) { mbuf[i + 1] = 0x0F; mbuf[i + 2] = 0x00; dim++; i += 2; }  // width 3440->3840
            }
            FILE* lf = NULL; fopen_s(&lf, "airplay_debug.log", "a");
            if (lf) { fprintf(lf, "MySend /info: fps-swaps=%d dim-swaps=%d (30->60, 1440->2880, 3440->3840)\n", fps, dim); fclose(lf); }
            return g_realSend(s, mbuf, len, flags);
        }
    }
    return g_realSend(s, buf, len, flags);
}

// Patch airplay2dll.dll's import table so its send() calls land in MySend.
static bool HookImport(HMODULE dll, const char* funcName, void* newFunc, void** origOut)
{
    if (!dll) return false;
    BYTE* base = (BYTE*)dll;
    IMAGE_DOS_HEADER* dos = (IMAGE_DOS_HEADER*)base;
    IMAGE_NT_HEADERS* nt = (IMAGE_NT_HEADERS*)(base + dos->e_lfanew);
    IMAGE_DATA_DIRECTORY imp = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (!imp.VirtualAddress) return false;
    IMAGE_IMPORT_DESCRIPTOR* desc = (IMAGE_IMPORT_DESCRIPTOR*)(base + imp.VirtualAddress);
    for (; desc->Name; desc++) {
        IMAGE_THUNK_DATA* oft = (IMAGE_THUNK_DATA*)(base + (desc->OriginalFirstThunk ? desc->OriginalFirstThunk : desc->FirstThunk));
        IMAGE_THUNK_DATA* ft = (IMAGE_THUNK_DATA*)(base + desc->FirstThunk);
        for (; oft->u1.AddressOfData; oft++, ft++) {
            if (oft->u1.Ordinal & IMAGE_ORDINAL_FLAG64) continue;
            IMAGE_IMPORT_BY_NAME* ibn = (IMAGE_IMPORT_BY_NAME*)(base + oft->u1.AddressOfData);
            if (strcmp((const char*)ibn->Name, funcName) == 0) {
                if (origOut) *origOut = (void*)(uintptr_t)ft->u1.Function;
                DWORD op = 0;
                VirtualProtect(&ft->u1.Function, sizeof(void*), PAGE_READWRITE, &op);
                ft->u1.Function = (ULONGLONG)(uintptr_t)newFunc;
                VirtualProtect(&ft->u1.Function, sizeof(void*), op, &op);
                return true;
            }
        }
    }
    return false;
}
// send() is bound by ORDINAL (ws2_32!send = ord 19), so a name lookup can miss it.
static bool HookImportOrd(HMODULE dll, const char* modName, WORD ordinal, void* newFunc, void** origOut)
{
    if (!dll) return false;
    BYTE* base = (BYTE*)dll;
    IMAGE_DOS_HEADER* dos = (IMAGE_DOS_HEADER*)base;
    IMAGE_NT_HEADERS* nt = (IMAGE_NT_HEADERS*)(base + dos->e_lfanew);
    IMAGE_DATA_DIRECTORY imp = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (!imp.VirtualAddress) return false;
    IMAGE_IMPORT_DESCRIPTOR* desc = (IMAGE_IMPORT_DESCRIPTOR*)(base + imp.VirtualAddress);
    for (; desc->Name; desc++) {
        const char* m = (const char*)(base + desc->Name);
        if (_stricmp(m, modName) != 0) continue;
        IMAGE_THUNK_DATA* oft = (IMAGE_THUNK_DATA*)(base + (desc->OriginalFirstThunk ? desc->OriginalFirstThunk : desc->FirstThunk));
        IMAGE_THUNK_DATA* ft = (IMAGE_THUNK_DATA*)(base + desc->FirstThunk);
        for (; oft->u1.AddressOfData; oft++, ft++) {
            if ((oft->u1.Ordinal & IMAGE_ORDINAL_FLAG64) && (WORD)(oft->u1.Ordinal & 0xFFFF) == ordinal) {
                if (origOut) *origOut = (void*)(uintptr_t)ft->u1.Function;
                DWORD op = 0;
                VirtualProtect(&ft->u1.Function, sizeof(void*), PAGE_READWRITE, &op);
                ft->u1.Function = (ULONGLONG)(uintptr_t)newFunc;
                VirtualProtect(&ft->u1.Function, sizeof(void*), op, &op);
                return true;
            }
        }
    }
    return false;
}
static void InstallSendHook()
{
    HMODULE dll = GetModuleHandleA("airplay2dll.dll");
    bool h = HookImport(dll, "send", (void*)&MySend, (void**)&g_realSend);
    if (!h) h = HookImportOrd(dll, "WS2_32.dll", 19, (void*)&MySend, (void**)&g_realSend);
    FILE* lf = NULL; fopen_s(&lf, "airplay_debug.log", "a");
    if (lf) { fprintf(lf, "InstallSendHook: dll=%p send=%d\n", (void*)dll, h ? 1 : 0); fclose(lf); }
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
{
    (void)hInstance; (void)hPrevInstance; (void)lpCmdLine; (void)nCmdShow;
    { FILE* lf = NULL; fopen_s(&lf, "airplay_debug.log", "a");
      if (lf) { fprintf(lf, "=== AirPlayReceiver %s (base: xenos v1.1.2) starting ===\n", APP_VERSION); fclose(lf); } }
    ImmDisableIME((DWORD)-1);  // disable IME for the whole process so H/F hotkeys work in any input language
    InstallSendHook();         // our fork's 60fps + supersampling: rewrite the DLL's /info bplist
    _CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);

    // Enable per-monitor DPI awareness for sharp rendering on high-DPI displays
    // Use runtime loading since these APIs require Windows 10 1703+
    {
        HMODULE hUser32 = GetModuleHandle(TEXT("user32.dll"));
        if (hUser32) {
            // Try Per-Monitor V2 first (best quality, Windows 10 1703+)
            typedef BOOL(WINAPI* SetDpiAwarenessContextFn)(HANDLE);
            SetDpiAwarenessContextFn fnCtx = (SetDpiAwarenessContextFn)
                GetProcAddress(hUser32, "SetProcessDpiAwarenessContext");
            if (fnCtx) {
                fnCtx(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
            } else {
                // Fallback: basic DPI awareness (Vista+)
                typedef BOOL(WINAPI* SetProcessDPIAwareFn)();
                SetProcessDPIAwareFn fnDpi = (SetProcessDPIAwareFn)
                    GetProcAddress(hUser32, "SetProcessDPIAware");
                if (fnDpi) fnDpi();
            }
        }
    }

    // Register atexit handler as fallback
    atexit(AtExitHandler);

    // Get default device name (PC name)
    char hostName[512] = { 0 };
    WSADATA wsaData;
    WSAStartup(MAKEWORD(2, 2), &wsaData);
    gethostname(hostName, sizeof(hostName) - 1);
    if (strlen(hostName) == 0) {
        DWORD n = sizeof(hostName) - 1;
        if (::GetComputerNameA(hostName, &n)) {
            if (n > 0 && n < sizeof(hostName)) {
                hostName[n] = '\0';
            }
        }
    }
    if (strlen(hostName) == 0) {
        strcpy_s(hostName, sizeof(hostName), "AirPlay Server");
    }

    // Check Bonjour Service (required for mDNS device discovery)
    {
        SC_HANDLE hSCM = OpenSCManagerA(NULL, NULL, SC_MANAGER_CONNECT);
        if (hSCM)
        {
            // First try with start permission, fall back to query-only
            // (SERVICE_START requires admin — don't let that look like "not installed")
            bool bCanStart = true;
            SC_HANDLE hSvc = OpenServiceA(hSCM, "Bonjour Service",
                SERVICE_QUERY_STATUS | SERVICE_START);
            if (!hSvc)
            {
                bCanStart = false;
                hSvc = OpenServiceA(hSCM, "Bonjour Service", SERVICE_QUERY_STATUS);
            }

            if (!hSvc)
            {
                // Truly not installed
                CloseServiceHandle(hSCM);
                int choice = MessageBoxA(NULL,
                    "Apple Bonjour is not installed.\n\n"
                    "Bonjour is required for AirPlay device discovery.\n\n"
                    "Click OK to open the Bonjour download page, or Cancel to exit.",
                    "AirPlay Server - Bonjour Not Found",
                    MB_OKCANCEL | MB_ICONWARNING);
                if (choice == IDOK)
                    ShellExecuteA(NULL, "open", "https://support.apple.com/106380",
                        NULL, NULL, SW_SHOWNORMAL);
                WSACleanup();
                return 1;
            }

            // Service exists — check its state
            SERVICE_STATUS ss = {};
            if (QueryServiceStatus(hSvc, &ss))
            {
                if (ss.dwCurrentState == SERVICE_STOPPED ||
                    ss.dwCurrentState == SERVICE_PAUSED)
                {
                    if (bCanStart)
                    {
                        // Try to start it
                        if (!StartServiceA(hSvc, 0, NULL))
                        {
                            DWORD err = GetLastError();
                            if (err != ERROR_SERVICE_ALREADY_RUNNING)
                            {
                                char msg[256];
                                _snprintf_s(msg, sizeof(msg), _TRUNCATE,
                                    "Bonjour Service could not be started (error %lu).\n\n"
                                    "Try running as Administrator, or start the service manually via services.msc.",
                                    err);
                                MessageBoxA(NULL, msg, "AirPlay Server - Bonjour Error",
                                    MB_OK | MB_ICONWARNING);
                                CloseServiceHandle(hSvc);
                                CloseServiceHandle(hSCM);
                                WSACleanup();
                                return 1;
                            }
                        }
                        else
                        {
                            // Wait up to 5 seconds for it to reach SERVICE_RUNNING
                            for (int i = 0; i < 50; i++)
                            {
                                Sleep(100);
                                if (QueryServiceStatus(hSvc, &ss) &&
                                    ss.dwCurrentState == SERVICE_RUNNING)
                                    break;
                            }
                        }
                    }
                    else
                    {
                        // No permission to start — ask user to do it manually
                        MessageBoxA(NULL,
                            "Bonjour Service is installed but not running.\n\n"
                            "Try running as Administrator, or start the service manually via services.msc.",
                            "AirPlay Server - Bonjour Stopped",
                            MB_OK | MB_ICONWARNING);
                        CloseServiceHandle(hSvc);
                        CloseServiceHandle(hSCM);
                        WSACleanup();
                        return 1;
                    }
                }
            }

            CloseServiceHandle(hSvc);
            CloseServiceHandle(hSCM);
        }
    }

    CSDLPlayer player;
    g_pPlayer = &player;  // Set global pointer for cleanup handlers
    player.setServerName(hostName);

    if (!player.init()) {
        g_pPlayer = NULL;
        return 1;
    }

    player.loopEvents();

    // Clear global pointer before player is destroyed
    g_pPlayer = NULL;

    return 0;
}
