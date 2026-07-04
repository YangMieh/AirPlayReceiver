// AirPlayServer.cpp : AirPlay Receiver - Main Program
// Starts automatically and waits for AirPlay connections.
// Window shows home screen with ImGui UI.
//
#include <windows.h>
#include <signal.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <intrin.h>
#include <imm.h>
#pragma comment(lib, "imm32.lib")
#include "Airplay2Head.h"
#include "CAirServerCallback.h"
#include "SDL.h"
#include "CSDLPlayer.h"

// Crash logger: records the faulting module + offset so we can pinpoint any crash
// (avcodec = decode, SDL2 = render, airplay2dll = protocol/decode, AirPlayServer = our code).
static void LogCrashLine(const char* tag, unsigned long code, void* addr)
{
    FILE* f = NULL;
    fopen_s(&f, "airplay_crash.log", "a");
    if (f) {
        HMODULE hm = NULL;
        char modName[MAX_PATH] = "?";
        if (addr) {
            GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                (LPCSTR)addr, &hm);
            if (hm) GetModuleFileNameA(hm, modName, MAX_PATH);
        }
        uintptr_t off = hm ? ((uintptr_t)addr - (uintptr_t)hm) : 0;
        SYSTEMTIME st; GetLocalTime(&st);
        fprintf(f, "[%02d:%02d:%02d] %s code=0x%08lX addr=%p module=%s +0x%IX\n",
            st.wHour, st.wMinute, st.wSecond, tag, code, addr, modName, off);
        fclose(f);
    }
}
static LONG WINAPI CrashHandler(EXCEPTION_POINTERS* ep)
{
    LogCrashLine("UNHANDLED", (unsigned long)ep->ExceptionRecord->ExceptionCode, ep->ExceptionRecord->ExceptionAddress);
    return EXCEPTION_EXECUTE_HANDLER;
}
// Vectored handler runs FIRST-chance, catching fatal exceptions the unhandled filter misses.
static LONG WINAPI VectoredHandler(EXCEPTION_POINTERS* ep)
{
    unsigned long code = (unsigned long)ep->ExceptionRecord->ExceptionCode;
    // Only log severe/fatal codes (access violation, stack overflow, heap/stack-cookie corruption).
    if (code == 0xC0000005 || code == 0xC00000FD || code == 0xC0000409 || code == 0xC0000374) {
        LogCrashLine("VEH", code, ep->ExceptionRecord->ExceptionAddress);
    }
    return EXCEPTION_CONTINUE_SEARCH;
}
static void InvalidParamHandler(const wchar_t*, const wchar_t*, const wchar_t*, unsigned int, uintptr_t)
{
    LogCrashLine("CRT_INVALID_PARAM", 0, _ReturnAddress());
}
static void AbortHandler(int) { LogCrashLine("CRT_ABORT", 0, _ReturnAddress()); }

// --- Process-exit guard --------------------------------------------------
// The prebuilt airplay2dll.dll terminates the whole app when the phone switches
// from screen-mirroring to AirPlay VIDEO mode (YouTube/Netflix casting). Every
// exit path (ExitProcess / exit / _exit / TerminateProcess) funnels through
// ntdll!NtTerminateProcess, so we hook THAT: the main thread's own shutdown is
// allowed through, but a background (DLL) thread trying to kill the process is
// blocked so the app stays alive and keeps mirroring.
static DWORD g_mainThreadId = 0;
static BYTE* g_ntTP = NULL;
static BYTE  g_ntTPorig[16];
typedef LONG (NTAPI *NtTerminateProcess_t)(HANDLE, LONG);

static LONG NTAPI MyNtTerminateProcess(HANDLE h, LONG status)
{
    bool selfProc = (h == NULL || h == (HANDLE)(LONG_PTR)-1);
    if (selfProc && GetCurrentThreadId() != g_mainThreadId) {
        LogCrashLine("BLOCKED_NtTerminateProcess_bg", (unsigned long)status, _ReturnAddress());
        for (;;) Sleep(1000);  // keep the app alive
    }
    // Allowed: restore the original bytes and call through.
    DWORD op = 0;
    VirtualProtect(g_ntTP, 16, PAGE_EXECUTE_READWRITE, &op);
    memcpy(g_ntTP, g_ntTPorig, 16);
    VirtualProtect(g_ntTP, 16, op, &op);
    FlushInstructionCache(GetCurrentProcess(), g_ntTP, 16);
    return ((NtTerminateProcess_t)g_ntTP)(h, status);
}
static void InstallExitProcessGuard()
{
    HMODULE nt = GetModuleHandleA("ntdll.dll");
    if (!nt) return;
    g_ntTP = (BYTE*)GetProcAddress(nt, "NtTerminateProcess");
    if (!g_ntTP) return;
    memcpy(g_ntTPorig, g_ntTP, 16);
    DWORD op = 0;
    if (!VirtualProtect(g_ntTP, 12, PAGE_EXECUTE_READWRITE, &op)) return;
    g_ntTP[0] = 0x48; g_ntTP[1] = 0xB8;                 // mov rax, imm64
    *(void**)(g_ntTP + 2) = (void*)&MyNtTerminateProcess;
    g_ntTP[10] = 0xFF; g_ntTP[11] = 0xE0;               // jmp rax
    VirtualProtect(g_ntTP, 12, op, &op);
    FlushInstructionCache(GetCurrentProcess(), g_ntTP, 12);
}

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
    { FILE* lf = NULL; fopen_s(&lf, "airplay_debug.log", "a");
      if (lf) { fprintf(lf, ">>> AtExitHandler called (process is exiting via exit()/return)\n"); fclose(lf); } }
    CleanupAndShutdown();
}

// --- Force-mirror experiment ---------------------------------------------
// Hide the AirPlay VIDEO capability so apps like YouTube fall back to screen
// mirroring instead of casting. We IAT-hook the DLL's call to dnssd's
// DNSServiceRegister and clear the "video" bits from the features flag in the
// advertised TXT record.
typedef int (WINAPI *DNSServiceRegister_t)(void*, DWORD, DWORD, const char*, const char*,
    const char*, const char*, unsigned short, unsigned short, const void*, void*, void*);
static DNSServiceRegister_t g_realDNSReg = NULL;

static int WINAPI MyDNSServiceRegister(void* sdRef, DWORD flags, DWORD ifIdx, const char* name,
    const char* regtype, const char* domain, const char* host, unsigned short port,
    unsigned short txtLen, const void* txtRecord, void* cb, void* ctx)
{
    static BYTE buf[2048];
    const void* useTxt = txtRecord;
    if (txtRecord && txtLen > 0 && txtLen <= sizeof(buf)) {
        memcpy(buf, txtRecord, txtLen);
        for (int i = 0; i + 19 < (int)txtLen; i++) {
            if (memcmp(buf + i, "features=0x", 11) == 0) {
                char hex[9] = { 0 }; memcpy(hex, buf + i + 11, 8);
                unsigned long v = strtoul(hex, NULL, 16);
                unsigned long nv = v & ~0x0000003FUL;  // clear bits0-5 (video/photo/fairplay/volctl/HLS/slideshow), keep screen(b7)+audio(b9)
                char nh[9]; sprintf_s(nh, "%08lX", nv);
                memcpy(buf + i + 11, nh, 8);
                FILE* lf = NULL; fopen_s(&lf, "airplay_debug.log", "a");
                if (lf) { fprintf(lf, "DNSReg %s: features 0x%s -> 0x%s\n", regtype ? regtype : "?", hex, nh); fclose(lf); }
                break;
            }
        }
        useTxt = buf;
    }
    return g_realDNSReg(sdRef, flags, ifIdx, name, regtype, domain, host, port, txtLen, useTxt, cb, ctx);
}
// dnssd is loaded dynamically via GetProcAddress, so we hook GetProcAddress in
// airplay2dll's IAT and return our DNSServiceRegister wrapper when it is requested.
typedef FARPROC (WINAPI *GetProcAddress_t)(HMODULE, LPCSTR);
static GetProcAddress_t g_realGPA = NULL;
static FARPROC WINAPI MyGetProcAddress(HMODULE h, LPCSTR name)
{
    FARPROC real = g_realGPA(h, name);
    if (real && name && (uintptr_t)name > 0xFFFF && strcmp(name, "DNSServiceRegister") == 0) {
        g_realDNSReg = (DNSServiceRegister_t)real;
        return (FARPROC)&MyDNSServiceRegister;
    }
    return real;
}
// Intercept the /info HTTP response the DLL sends and bump the advertised display
// maxFPS/refreshRate from 30 to 60 (bplist int 0x10 0x1E -> 0x10 0x3C) to try to
// make the iPhone mirror at 60fps. [experiment]
typedef int (WINAPI *send_t)(SOCKET, const char*, int, int);
static send_t g_realSend = NULL;
static LONG g_sendLogCount = 0;
static int WINAPI MySend(SOCKET s, const char* buf, int len, int flags)
{
    if (buf && InterlockedIncrement(&g_sendLogCount) <= 40) {
        char snip[33]; int n = len < 32 ? len : 32;
        for (int i = 0; i < n; i++) { char c = buf[i]; snip[i] = (c >= 32 && c < 127) ? c : '.'; }
        snip[n] = 0;
        FILE* lf = NULL; fopen_s(&lf, "airplay_debug.log", "a");
        if (lf) { fprintf(lf, "send len=%d: %s\n", len, snip); fclose(lf); }
    }
    if (buf && len > 40 && len < 8192) {
        bool hasBp = false, hasFps = false;
        for (int i = 0; i + 8 <= len; i++) { if (memcmp(buf + i, "bplist00", 8) == 0) { hasBp = true; break; } }
        if (hasBp) for (int i = 0; i + 6 <= len; i++) { if (memcmp(buf + i, "maxFPS", 6) == 0) { hasFps = true; break; } }
        if (hasBp && hasFps) {
            // one-time dump of the /info bplist so we can decode the advertised display
            static bool dumped = false;
            if (!dumped) {
                dumped = true;
                int bpOff = 0; for (int i = 0; i + 8 <= len; i++) { if (memcmp(buf + i, "bplist00", 8) == 0) { bpOff = i; break; } }
                FILE* df = NULL; fopen_s(&df, "airplay_info.bin", "wb");
                if (df) { fwrite(buf + bpOff, 1, len - bpOff, df); fclose(df); }
            }
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
// Hook an import that is bound by ORDINAL from a specific module (e.g. ws2_32!send = ord 19).
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
static void InstallDNSHook()
{
    HMODULE dll = GetModuleHandleA("airplay2dll.dll");
    bool h1 = HookImport(dll, "GetProcAddress", (void*)&MyGetProcAddress, (void**)&g_realGPA);
    bool h2 = HookImport(dll, "send", (void*)&MySend, (void**)&g_realSend);
    if (!h2) h2 = HookImportOrd(dll, "WS2_32.dll", 19, (void*)&MySend, (void**)&g_realSend);
    FILE* lf = NULL; fopen_s(&lf, "airplay_debug.log", "a");
    if (lf) { fprintf(lf, "InstallDNSHook: dll=%p GetProcAddress=%d send=%d\n", (void*)dll, h1 ? 1 : 0, h2 ? 1 : 0); fclose(lf); }
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
{
    (void)hInstance; (void)hPrevInstance; (void)lpCmdLine; (void)nCmdShow;
    ImmDisableIME((DWORD)-1);  // disable IME for the whole process so H/F hotkeys work in any input language
    g_mainThreadId = GetCurrentThreadId();
    SetUnhandledExceptionFilter(CrashHandler);
    AddVectoredExceptionHandler(1, VectoredHandler);
    _set_invalid_parameter_handler(InvalidParamHandler);
    signal(SIGABRT, AbortHandler);
    InstallExitProcessGuard();
    InstallDNSHook();   // force-mirror experiment: hide video capability
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

    { FILE* lf = NULL; fopen_s(&lf, "airplay_debug.log", "a");
      if (lf) { fprintf(lf, ">>> loopEvents() returned normally (SDL_QUIT path)\n"); fclose(lf); } }

    // Clear global pointer before player is destroyed
    g_pPlayer = NULL;

    return 0;
}
