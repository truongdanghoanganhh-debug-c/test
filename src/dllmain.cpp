/*
 * dllmain.cpp — NullWare DLL Entry Point
 *
 * Spawns main thread on DLL_PROCESS_ATTACH.
 * Main thread: waits for game module, initializes hooks, waits for unload key.
 */

#include <Windows.h>
#include <cstdio>

#include "hooks.h"

static HMODULE g_hModule = nullptr;

static DWORD WINAPI MainThread(LPVOID) {
    // ── Allocate debug console ──
    AllocConsole();
    FILE* fOut = nullptr;
    FILE* fErr = nullptr;
    freopen_s(&fOut, "CONOUT$", "w", stdout);
    freopen_s(&fErr, "CONOUT$", "w", stderr);

    printf("╔══════════════════════════════════════╗\n");
    printf("║         NullWare Internal            ║\n");
    printf("╚══════════════════════════════════════╝\n\n");

    // ── Wait for game module ──
    printf("[NullWare] Waiting for VALORANT-Win64-Shipping.exe...\n");
    HMODULE hGame = nullptr;
    for (int i = 0; i < 120; i++) {  // 60 seconds max
        hGame = GetModuleHandleW(L"VALORANT-Win64-Shipping.exe");
        if (hGame) break;
        Sleep(500);
    }

    if (!hGame) {
        printf("[NullWare] FATAL: Game module not found after 60 seconds!\n");
        Beep(300, 500);
        Sleep(3000);
        if (fOut) fclose(fOut);
        if (fErr) fclose(fErr);
        FreeConsole();
        FreeLibraryAndExitThread(g_hModule, 1);
        return 1;
    }

    printf("[NullWare] Game module found at 0x%p\n", hGame);
    printf("[NullWare] Waiting for game initialization (3s)...\n");
    Sleep(3000);

    // ── Initialize hooks ──
    printf("[NullWare] Initializing hooks...\n");
    if (Hooks::Init()) {
        printf("[NullWare] ✓ All hooks installed!\n");
        printf("[NullWare] INSERT = Toggle Menu | END = Unload\n\n");
        Beep(800, 200);
        Beep(600, 200);  // Success beep
    } else {
        printf("[NullWare] ✗ Hook initialization failed!\n");
        Beep(300, 500);  // Failure beep
        Sleep(3000);
        if (fOut) fclose(fOut);
        if (fErr) fclose(fErr);
        FreeConsole();
        FreeLibraryAndExitThread(g_hModule, 1);
        return 1;
    }

    // ── Wait for unload key (END) ──
    while (!(GetAsyncKeyState(VK_END) & 1)) {
        Sleep(100);
    }

    // ── Shutdown ──
    printf("[NullWare] Shutting down...\n");
    Hooks::Shutdown();
    Sleep(500);

    printf("[NullWare] Goodbye.\n");
    if (fOut) fclose(fOut);
    if (fErr) fclose(fErr);
    FreeConsole();
    FreeLibraryAndExitThread(g_hModule, 0);
    return 0;
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        g_hModule = hModule;
        DisableThreadLibraryCalls(hModule);
        CreateThread(nullptr, 0, MainThread, nullptr, 0, nullptr);
    }
    return TRUE;
}
