/*
 * emu_installer.cpp — VGC Emulator Installer v5.0
 *
 * Matches the Tozic installer behavior EXACTLY (reverse-engineered from strings):
 *   1. Set DevOverrideEnable = 1
 *   2. Kill Vanguard processes
 *   3. Uninstall Vanguard (installer.exe --quiet)
 *   4. Delete leftover services
 *   5. Copy fake vgc.exe to %TEMP%
 *   6. sc create vgc binPath= "%TEMP%\vgc.exe" start= demand DisplayName= "vService"
 *   7. sc sdset vgc <exact SDDL from Tozic>
 *   8. Set DevOverrideEnable = 1 again
 *   9. Restart
 *
 * Build: cl /EHsc /O2 /MT emu_installer.cpp advapi32.lib shell32.lib /Fe:emu_installer.exe
 * Run:   As Administrator. Close Riot Client first. Have Vanguard open.
 */

#define WIN32_LEAN_AND_MEAN
#define _CRT_SECURE_NO_WARNINGS
#include <windows.h>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <filesystem>

namespace fs = std::filesystem;

// ============================================================================
// Console helpers
// ============================================================================

static HANDLE g_hConsole = INVALID_HANDLE_VALUE;

static void SetColor(WORD c) {
    if (g_hConsole == INVALID_HANDLE_VALUE)
        g_hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
    SetConsoleTextAttribute(g_hConsole, c);
}

static void PrintOK(const wchar_t* m) {
    SetColor(FOREGROUND_GREEN | FOREGROUND_INTENSITY);
    wprintf(L"%s", m);
    SetColor(FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);
}

static void PrintErr(const wchar_t* m) {
    SetColor(FOREGROUND_RED | FOREGROUND_INTENSITY);
    wprintf(L"%s", m);
    SetColor(FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);
}

static void PrintWarn(const wchar_t* m) {
    SetColor(FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_INTENSITY);
    wprintf(L"%s", m);
    SetColor(FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);
}

static void PrintInfo(const wchar_t* m) {
    SetColor(FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);
    wprintf(L"%s", m);
}

// ============================================================================
// Run a command via cmd.exe and wait for it
// ============================================================================

static DWORD RunCmd(const std::wstring& cmd, DWORD timeoutMs = 30000) {
    STARTUPINFOW si = {};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi = {};

    std::wstring full = L"/c " + cmd;
    wchar_t* buf = _wcsdup(full.c_str());

    if (!CreateProcessW(L"C:\\Windows\\System32\\cmd.exe", buf,
            nullptr, nullptr, FALSE, CREATE_NO_WINDOW,
            nullptr, nullptr, &si, &pi)) {
        free(buf);
        return (DWORD)-1;
    }
    free(buf);

    WaitForSingleObject(pi.hProcess, timeoutMs);
    DWORD exitCode = (DWORD)-1;
    GetExitCodeProcess(pi.hProcess, &exitCode);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    return exitCode;
}

// ============================================================================
// Set DevOverrideEnable = 1
// ============================================================================

static bool SetDevOverrideEnable() {
    HKEY hKey;
    LONG result = RegCreateKeyExW(
        HKEY_LOCAL_MACHINE,
        L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Image File Execution Options",
        0, nullptr, REG_OPTION_NON_VOLATILE,
        KEY_SET_VALUE, nullptr, &hKey, nullptr);

    if (result != ERROR_SUCCESS) {
        wprintf(L" [ERROR] Cannot open IFEO key (error %ld)\n", result);
        return false;
    }

    DWORD value = 1;
    result = RegSetValueExW(hKey, L"DevOverrideEnable", 0, REG_DWORD,
        (const BYTE*)&value, sizeof(DWORD));
    RegCloseKey(hKey);

    if (result == ERROR_SUCCESS) {
        PrintOK(L" [OK] DevOverrideEnable = 1\n");
        return true;
    }
    PrintErr(L" [ERROR] Failed to set DevOverrideEnable\n");
    return false;
}

// ============================================================================
// Kill Vanguard processes
// ============================================================================

static void KillVanguardProcesses() {
    RunCmd(L"taskkill /F /IM vgtray.exe >nul 2>&1", 5000);
    RunCmd(L"taskkill /F /IM vgc.exe >nul 2>&1", 5000);
    PrintOK(L" [OK] Vanguard processes killed.\n");
}

// ============================================================================
// Find Vanguard directory
// ============================================================================

static fs::path FindVanguardDir() {
    // Check common paths
    wchar_t progFiles[MAX_PATH];
    if (GetEnvironmentVariableW(L"ProgramFiles", progFiles, MAX_PATH)) {
        fs::path p = fs::path(progFiles) / "Riot Vanguard";
        if (fs::exists(p)) return p;
    }

    if (fs::exists(L"C:\\Program Files\\Riot Vanguard"))
        return L"C:\\Program Files\\Riot Vanguard";
    if (fs::exists(L"D:\\Program Files\\Riot Vanguard"))
        return L"D:\\Program Files\\Riot Vanguard";

    return fs::path();
}

// ============================================================================
// Uninstall Vanguard using its own installer --quiet
// (exact same method as Tozic: uses installer.exe --quiet)
// ============================================================================

static bool UninstallVanguard() {
    fs::path vgDir = FindVanguardDir();
    fs::path installer;

    if (!vgDir.empty()) {
        installer = vgDir / "installer.exe";
    }

    if (!installer.empty() && fs::exists(installer)) {
        PrintInfo(L" [INFO] Attempting to uninstall Vanguard...\n");
        wprintf(L"        Using: %s --quiet\n", installer.wstring().c_str());

        STARTUPINFOW si = {};
        si.cb = sizeof(si);
        PROCESS_INFORMATION pi = {};

        std::wstring cmdLine = L"\"" + installer.wstring() + L"\" --quiet";
        wchar_t* buf = _wcsdup(cmdLine.c_str());

        if (CreateProcessW(nullptr, buf, nullptr, nullptr, FALSE,
                0, nullptr, nullptr, &si, &pi)) {
            WaitForSingleObject(pi.hProcess, 60000); // 60 sec timeout
            DWORD exitCode = 0;
            GetExitCodeProcess(pi.hProcess, &exitCode);
            CloseHandle(pi.hProcess);
            CloseHandle(pi.hThread);
            wprintf(L"        Exit code: %lu\n", exitCode);
            PrintOK(L" [SUCCESS] Vanguard uninstallation completed.\n");
        } else {
            PrintErr(L" [ERROR] Failed to start Vanguard uninstallation process.\n");
        }
        free(buf);
    } else {
        PrintWarn(L" [WARN] Vanguard installer not found.\n");
        PrintWarn(L"        Please manually uninstall Vanguard with Revo uninstaller\n");
        PrintWarn(L"        or similar tool.\n");
    }

    // Clean up leftover services
    PrintInfo(L" [INFO] Cleaning up services...\n");
    RunCmd(L"sc stop vgc >nul 2>&1", 10000);
    RunCmd(L"sc stop vgk >nul 2>&1", 10000);
    RunCmd(L"sc delete vgc >nul 2>&1", 5000);
    RunCmd(L"sc delete vgk >nul 2>&1", 5000);
    PrintOK(L" [OK] Services cleaned up.\n");

    return true;
}

// ============================================================================
// Copy fake vgc.exe to %TEMP% and register service
// (Tozic puts it in temp — that's why clearing %temp% removes the emu)
// ============================================================================

static bool RegisterFakeVgc() {
    // Find our fake vgc.exe (next to this installer)
    wchar_t ourExe[MAX_PATH];
    GetModuleFileNameW(nullptr, ourExe, MAX_PATH);
    fs::path srcVgc = fs::path(ourExe).parent_path() / "vgc.exe";

    if (!fs::exists(srcVgc)) {
        PrintErr(L" [ERROR] vgc.exe not found next to this installer!\n");
        wprintf(L"         Expected: %s\n", srcVgc.wstring().c_str());
        PrintErr(L" [ERROR] Failed to register zombie Vanguard Client.\n");
        PrintErr(L"         Please make sure FakeVgc.exe is placed in same directory\n");
        PrintErr(L"         as this app and try running this app as administrator\n");
        PrintErr(L"         if issue persists.\n");
        return false;
    }

    // Copy to %TEMP% (same as Tozic)
    wchar_t tempPath[MAX_PATH];
    GetTempPathW(MAX_PATH, tempPath);
    fs::path destVgc = fs::path(tempPath) / "vgc.exe";

    std::error_code ec;
    fs::copy_file(srcVgc, destVgc, fs::copy_options::overwrite_existing, ec);
    if (ec) {
        PrintWarn(L" [WARN] Could not copy to temp, using original path.\n");
        destVgc = srcVgc;
    } else {
        wprintf(L" [OK] Copied vgc.exe → %s\n", destVgc.wstring().c_str());
    }

    // Also copy the DLLs if they exist (runtime dependencies)
    const wchar_t* dlls[] = {
        L"msvcp140.dll", L"vcruntime140.dll", L"vcruntime140_1.dll",
        L"msvcp140d.dll", L"vcruntime140d.dll", L"vcruntime140_1d.dll",
        L"ucrtbased.dll", nullptr
    };
    for (int i = 0; dlls[i]; i++) {
        fs::path dllSrc = fs::path(ourExe).parent_path() / dlls[i];
        if (fs::exists(dllSrc)) {
            fs::copy_file(dllSrc, fs::path(tempPath) / dlls[i],
                fs::copy_options::overwrite_existing, ec);
        }
    }

    // Register service using sc create (EXACT Tozic command)
    // sc create vgc binPath= "<path>" start= demand DisplayName= "vService"
    std::wstring scCreateCmd =
        L"sc create vgc binPath= \"" + destVgc.wstring() +
        L"\" start= demand DisplayName= \"vService\"";

    wprintf(L" [INFO] Running: %s\n", scCreateCmd.c_str());
    DWORD result = RunCmd(scCreateCmd, 10000);
    if (result == 0) {
        PrintOK(L" [OK] Service 'vgc' created (DisplayName=vService).\n");
    } else {
        wprintf(L" [WARN] sc create returned %lu\n", result);
        // Might already exist, try to reconfigure
        std::wstring scConfigCmd =
            L"sc config vgc binPath= \"" + destVgc.wstring() +
            L"\" start= demand DisplayName= \"vService\"";
        RunCmd(scConfigCmd, 5000);
        PrintWarn(L" [OK] Reconfigured existing vgc service.\n");
    }

    // Set permissions (EXACT Tozic SDDL — includes S-1-5-32-545 Users group)
    std::wstring scSdsetCmd =
        L"sc sdset vgc D:(A;;CCLCSWRPWPDTLOCRRC;;;SY)"
        L"(A;;CCDCLCSWRPWPDTLOCRSDRCWDWO;;;BA)"
        L"(A;;CCLCSWLOCRRC;;;IU)"
        L"(A;;CCLCSWLOCRRC;;;SU)"
        L"(A;;RPLOCRRC;;;S-1-5-32-545)";

    wprintf(L" [INFO] Setting service security...\n");
    result = RunCmd(scSdsetCmd, 5000);
    if (result == 0) {
        PrintOK(L" [OK] Service security descriptor set.\n");
    } else {
        SetColor(FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_INTENSITY);
        wprintf(L" [WARN] sdset returned %lu (may still work)\n", result);
        SetColor(FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);
    }

    return true;
}

// ============================================================================
// Verify
// ============================================================================

static void Verify() {
    // Check DevOverrideEnable
    HKEY hKey;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE,
            L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Image File Execution Options",
            0, KEY_QUERY_VALUE, &hKey) == ERROR_SUCCESS) {
        DWORD val = 0, sz = sizeof(DWORD), type;
        if (RegQueryValueExW(hKey, L"DevOverrideEnable", nullptr, &type,
                (BYTE*)&val, &sz) == ERROR_SUCCESS) {
            wprintf(L"   DevOverrideEnable = %lu %s\n", val,
                val == 1 ? L"[OK]" : L"[WRONG - should be 1]");
        }
        RegCloseKey(hKey);
    }

    // Check vgc service
    wprintf(L"\n");
    RunCmd(L"sc qc vgc", 5000);
}

// ============================================================================
// Entry
// ============================================================================

int wmain(int argc, wchar_t* argv[]) {
    SetConsoleTitleW(L"VGC Emulator Installer v5.0");
    g_hConsole = GetStdHandle(STD_OUTPUT_HANDLE);

    wprintf(L"\n");
    wprintf(L"  +============================================+\n");
    wprintf(L"  |       VGC Emulator Installer v5.0          |\n");
    wprintf(L"  |   (Tozic-compatible method)                |\n");
    wprintf(L"  +============================================+\n\n");

    PrintWarn(L"  [!] Close Riot Client before running!\n");
    PrintWarn(L"  [!] Have Vanguard installed/open!\n\n");

    // Step 1
    PrintInfo(L" === Step 1: DevOverrideEnable = 1 ===\n");
    SetDevOverrideEnable();

    // Step 2
    PrintInfo(L"\n === Step 2: Kill Vanguard processes ===\n");
    KillVanguardProcesses();

    // Step 3
    PrintInfo(L"\n === Step 3: Uninstall Vanguard ===\n");
    UninstallVanguard();

    // Step 4: Set DevOverrideEnable again (in case uninstaller reset it)
    PrintInfo(L"\n === Step 4: Re-set DevOverrideEnable = 1 ===\n");
    SetDevOverrideEnable();

    // Step 5
    PrintInfo(L"\n === Step 5: Register fake vgc service ===\n");
    if (!RegisterFakeVgc()) {
        PrintErr(L"\n INSTALLATION FAILED.\n\n");
        system("pause > nul");
        return 1;
    }

    // Verify
    PrintInfo(L"\n === Verification ===\n");
    Verify();

    // Done
    wprintf(L"\n");
    wprintf(L"  +============================================+\n");
    PrintOK( L"  |         INSTALLATION COMPLETE!             |\n");
    wprintf(L"  +============================================+\n\n");

    PrintOK( L"   RESTART YOUR PC NOW!\n\n");

    PrintInfo(L"   After restart:\n");
    PrintInfo(L"   - Open Riot Client -> Valorant\n");
    PrintInfo(L"   - No Vanguard download prompt\n");
    PrintInfo(L"   - No kernel anti-cheat = any injector works\n\n");

    PrintInfo(L"   To uninstall the emulator:\n");
    PrintInfo(L"   - Delete files from where you placed them\n");
    PrintInfo(L"   - Clear %%TEMP%%\n");
    PrintInfo(L"   - sc delete vgc\n");
    PrintInfo(L"   - Set DevOverrideEnable back to 0\n\n");

    wprintf(L" Press any key to exit...\n");
    system("pause > nul");
    return 0;
}
