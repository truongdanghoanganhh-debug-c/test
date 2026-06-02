/*
 * fake_vgc.cpp — VGC Pipe Emulator (Windows Service)
 *
 * Impersonates the Riot Vanguard Client (vgc) service.
 * Hosts a named pipe that responds to VGK (kernel driver) heartbeats,
 * allowing Valorant to run without real Vanguard installed.
 *
 * Build: cl /EHsc /O2 /MT fake_vgc.cpp advapi32.lib /Fe:vgc.exe
 */

#define WIN32_LEAN_AND_MEAN
#define _CRT_SECURE_NO_WARNINGS
#include <windows.h>
#include <sddl.h>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <intrin.h>
#include <process.h>

// ============================================================================
// Constants (extracted from reverse engineering)
// ============================================================================

static const wchar_t PIPE_NAME[]    = L"\\\\.\\pipe\\933823D3-C77B-4BAE-89D7-A92B567236BC";
static const wchar_t PIPE_DACL[]    = L"D:(A;OICI;GRGW;;;WD)";
static const wchar_t SERVICE_NAME[] = L"vgc";

static constexpr DWORD VGC_MAGIC        = 0x56474320;  // 'VGC '
static constexpr DWORD VGC_RESP_MAGIC   = 0x564743C0;  // response magic (VGC_MAGIC + 0xA0)
static constexpr DWORD TLS_MAGIC        = 0x47534300;  // 'GSC\0'
static constexpr DWORD PIPE_OPEN_MODE   = PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED;
static constexpr DWORD PIPE_MODE_FLAGS  = PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE;
static constexpr DWORD PIPE_BUF_SIZE    = 0x1000;
static constexpr DWORD PIPE_MAX_INST    = 255;
static constexpr DWORD APPEND_INTERVAL  = 41;
static constexpr DWORD IDLE_TIMEOUT_MS  = 30000;    // 30 seconds
static constexpr int   MAX_SESSION_MIN  = 28;        // 28 minutes
static constexpr int   BROKEN_PIPE_MAX  = 3;
static constexpr int   OTHER_ERROR_MAX  = 8;

// ============================================================================
// Globals
// ============================================================================

static SERVICE_STATUS        g_ServiceStatus = {};
static SERVICE_STATUS_HANDLE g_hServiceStatus = nullptr;
static HANDLE                g_hStopEvent = nullptr;
static volatile bool         g_bStopping = false;
static volatile bool         g_bStopped  = false;
static bool                  g_bHeapNoiseInit = false;
static HANDLE                g_heapSlots[3] = {};
static ULONGLONG             g_lastSysInfoTick = 0;

// ============================================================================
// Polynomial hash constants (from ValidatePacket)
// ============================================================================

static const int32_t POS_MULT[] = {
    2050938145, 994064801, 326702913, 1954312449, 790803233,
    1700710369, 204809697, 1359805057, 1923893921, 1331628417,
    288867329, 821255521, 1043908673, 942687265, 864055905
};

static const int32_t NEG_MULT[] = {
    -1038517951, -1407692031, -2103705759, -1831214591,
    -869847455, -936010047, -1327864255, -1555599935,
    -300539359, -1718002783, -702813311, -331229983,
    -823560479, -859516991
};

// ============================================================================
// ValidatePacket — polynomial hash check over bytes [8..43]
// ============================================================================

static bool ValidatePacket(const uint8_t* buf, DWORD size) {
    if (size < 0x30) return false;
    if (*(const uint32_t*)buf != VGC_MAGIC) return false;
    if (*(const uint32_t*)(buf + 4) != 16) return false;

    // The original uses a complex polynomial hash. We replicate the exact check.
    uint8_t b0_hi8 = buf[1];       // BYTE1 of dword[0]
    uint8_t b0_hi16 = buf[2];      // BYTE2 of dword[0] (LOWORD high byte)
    uint8_t b0_top = buf[3];       // HIBYTE of dword[0]
    uint8_t b1_top = buf[7];       // HIBYTE of dword[1]

    int32_t hash = 0;
    // Positive terms
    hash += 2050938145 * (int32_t)b0_hi16;
    hash += 994064801  * (int32_t)buf[30];
    hash += 326702913  * (int32_t)buf[17];
    hash += 1954312449 * (int32_t)buf[35];
    hash += 790803233  * (int32_t)buf[18];
    hash += 1700710369 * (int32_t)buf[12];
    hash += 204809697  * (int32_t)buf[28];
    hash += 1359805057 * (int32_t)buf[23];
    hash += 1923893921 * (int32_t)buf[22];
    hash += 1331628417 * (int32_t)buf[31];
    hash += 288867329  * (int32_t)buf[11];

    // The big chain of multiplied sums
    hash += 821255521  * (int32_t)buf[32];
    hash += 1043908673 * (int32_t)buf[9];
    hash += 942687265  * (int32_t)buf[10];
    hash += 89247841   * (int32_t)(33 * b1_top + buf[8]);
    hash += 67801377   * (int32_t)(buf[34] + 33 * buf[33]);
    hash += 33 * (9771233 * (int32_t)b0_top
        + (int32_t)buf[42]
        + 33 * ((int32_t)buf[41]
            + 33 * ((int32_t)buf[40]
                + 33 * ((int32_t)buf[39] + 33 * ((int32_t)buf[38] + 33 * (int32_t)buf[37])))));

    // Negative terms
    hash -= 1407692031 * (int32_t)buf[19];
    hash -= 2103705759u * (int32_t)buf[16];
    hash -= 1831214591 * (int32_t)buf[27];
    hash -= 869847455  * (int32_t)buf[24];
    hash -= 936010047  * (int32_t)buf[21];
    hash -= 1327864255 * (int32_t)buf[25];
    hash -= 1555599935 * (int32_t)buf[29];
    hash -= 300539359  * (int32_t)buf[26];
    hash -= 1718002783 * (int32_t)buf[14];
    hash -= 702813311  * (int32_t)buf[15];
    hash -= 331229983  * (int32_t)buf[36];
    hash -= 823560479  * (int32_t)buf[20];
    hash -= 859516991  * (int32_t)buf[13];
    hash -= 1038517951 * (int32_t)b0_hi8;

    hash += (int32_t)buf[43];
    hash -= 1301956048;

    return hash == *(const int32_t*)(buf + 40);
}

// ============================================================================
// BuildResponse — XOR-scrambled 64-byte handshake reply
// ============================================================================

static bool BuildResponse(HANDLE hPipe, const uint8_t* pkt) {
    uint8_t resp[0x40] = {};

    // Header
    *(uint32_t*)resp = VGC_RESP_MAGIC;  // 0x564743C0
    *(uint32_t*)(resp + 4) = 17;

    // Copy dword[2] and dword[3] from request
    *(uint32_t*)(resp + 8)  = *(const uint32_t*)(pkt + 8);
    *(uint32_t*)(resp + 12) = *(const uint32_t*)(pkt + 12);

    // Store GetTickCount64 at offset 16
    ULONGLONG tick = GetTickCount64();
    *(ULONGLONG*)(resp + 16) = tick;

    // XOR layer 1: bytes [24..35] from request
    resp[24] = pkt[24];           // as-is
    resp[25] = pkt[25] ^ 0x55;
    resp[26] = pkt[26] ^ 0xAA;
    resp[27] = ~pkt[27];          // bitwise NOT
    resp[28] = pkt[28] ^ 0x54;
    resp[29] = pkt[29] ^ 0xA9;
    resp[30] = pkt[30] ^ 0xFE;
    resp[31] = pkt[31] ^ 0x53;
    resp[32] = pkt[32] ^ 0xA8;
    resp[33] = pkt[33] ^ 0xFD;
    resp[34] = pkt[34] ^ 0x52;
    resp[35] = pkt[35] ^ 0xA7;

    // XOR layer 2: additional transform bytes
    resp[36] = pkt[36] ^ 0xFC;
    resp[37] = pkt[37] ^ 0x51;
    resp[38] = pkt[38] ^ 0xA6;
    resp[39] = pkt[39] ^ 0xFB;

    // XOR layer 3: offset bytes for second key set
    resp[40] = pkt[24] ^ 0x50;
    resp[41] = pkt[25] ^ 0xA5;
    resp[42] = pkt[26] ^ 0xFA;
    resp[43] = pkt[27] ^ 0x4F;
    resp[44] = pkt[29] ^ 0xF9;
    resp[45] = pkt[30] ^ 0x4E;
    resp[46] = pkt[31] ^ 0xA3;
    resp[47] = pkt[32] ^ 0xF8;
    resp[48] = pkt[33] ^ 0x4D;
    resp[49] = pkt[34] ^ 0xA2;
    resp[50] = pkt[35] ^ 0xF7;
    resp[51] = pkt[36] ^ 0x4C;
    resp[52] = pkt[37] ^ 0xA1;
    resp[53] = pkt[38] ^ 0xF6;
    resp[54] = pkt[39] ^ 0x4B;
    resp[55] = pkt[28] ^ 0xA4;

    // Compute integrity hash for the response (same polynomial as validate)
    // Replicate the exact hash from BuildResponse decompilation
    int32_t rhash = 0;
    rhash += 864055905  * (int32_t)(*(uint32_t*)(resp + 8) & 0xFF);
    rhash += 807086657  * (int32_t)((*(uint32_t*)(resp + 8) >> 8) & 0xFF);
    rhash += 2046197153 * (int32_t)((*(uint32_t*)(resp + 12) >> 16) & 0xFF);
    rhash += 52012545   * (int32_t)(((*(uint32_t*)(resp + 8) >> 24) & 0xFF) + 33 * ((*(uint32_t*)(resp + 8) >> 16) & 0xFF));
    rhash += 88645985   * (int32_t)(((uint8_t)tick) + 33 * ((*(uint32_t*)(resp + 12) >> 24) & 0xFF));
    rhash += 89247841   * (int32_t)(resp[24] + 33 * ((tick >> 24) & 0xFF));
    rhash += 9771233    * (int32_t)(((tick >> 32) & 0xFF) + 33 * ((tick >> 24) & 0xFF));

    // Add the XOR'd bytes using the same multipliers
    rhash += 2050938145 * (int32_t)((tick >> 16) & 0xFF);
    rhash += 1043908673 * (int32_t)resp[25];
    rhash += 942687265  * (int32_t)resp[26];
    rhash += 288867329  * (int32_t)resp[27];
    rhash += 1700710369 * (int32_t)resp[28];
    rhash += 821255521  * (int32_t)resp[47];
    rhash += 67801377   * (int32_t)(resp[49] + 33 * resp[48]);
    rhash += 790803233  * (int32_t)resp[34];
    rhash += 326702913  * (int32_t)resp[33];
    rhash += 994064801  * (int32_t)resp[45];
    rhash += 1954312449 * (int32_t)resp[50];
    rhash += 204809697  * (int32_t)resp[55];
    rhash += 1359805057 * (int32_t)resp[38];
    rhash += 1923893921 * (int32_t)resp[37];
    rhash += 1331628417 * (int32_t)resp[46];
    rhash += 1185921    * (int32_t)(resp[54] + 33 * (resp[53] + 33 * resp[52]));

    rhash += 33 * ((int32_t)resp[42]
        + 33 * ((int32_t)resp[41]
            + 33 * ((int32_t)resp[40]
                + 33 * ((int32_t)resp[39] + 33 * ((int32_t)resp[38] + 33 * (int32_t)resp[37])))));

    // Negative terms
    rhash -= 1038517951 * (int32_t)((tick >> 8) & 0xFF);
    rhash -= 859516991  * (int32_t)resp[29];
    rhash -= 1407692031 * (int32_t)resp[35];
    rhash -= 2103705759u * (int32_t)resp[32];
    rhash -= 1831214591 * (int32_t)resp[43];
    rhash -= 869847455  * (int32_t)resp[40];
    rhash -= 1327864255 * (int32_t)resp[41];
    rhash -= 936010047  * (int32_t)resp[36];
    rhash -= 1555599935 * (int32_t)resp[44];
    rhash -= 300539359  * (int32_t)resp[42];
    rhash -= 1718002783 * (int32_t)resp[30];
    rhash -= 702813311  * (int32_t)resp[31];
    rhash -= 331229983  * (int32_t)resp[51];
    rhash -= 823560479  * (int32_t)resp[36];

    rhash -= 1593348959 * (int32_t)((tick >> 48) & 0xFF);
    rhash -= 1040908095 * (int32_t)((tick >> 40) & 0xFF);
    rhash -= 1194970687 * (int32_t)((*(uint32_t*)(resp + 12) >> 8) & 0xFF);
    rhash -= 779327007  * (int32_t)(*(uint32_t*)(resp + 12) & 0xFF);

    rhash += -1074808234 + 1431734823 + 522742816 + 1696545803 - 1651480335;
    rhash += (int32_t)resp[43];

    *(int32_t*)(resp + 56) = rhash;

    DWORD written = 0;
    return WriteFile(hPipe, resp, 0x40, &written, nullptr) != FALSE;
}

// ============================================================================
// HeapNoise — creates dummy heap allocations to mimic real VGC memory profile
// ============================================================================

static void GenerateHeapNoise() {
    if (g_bHeapNoiseInit) return;

    // Pattern bytes (0xCC fill from xmmword_140005580)
    uint8_t pattern[32];
    memset(pattern, 0xCC, sizeof(pattern));

    for (int s = 0; s < 3; s++) {
        g_heapSlots[s] = HeapCreate(0, 0, 0);
        for (int i = 0; i < 5; i++) {
            SIZE_T sz = (rand() % 0x2000) + 4096;
            void* p = HeapAlloc(g_heapSlots[s], 0, sz);
            if (!p) break;
            SIZE_T fillSz = (sz > 64) ? 64 : sz;
            memset(p, 0, fillSz);
            if (i == 0) {
                memcpy(p, pattern, (fillSz < 32) ? fillSz : 32);
            }
        }
    }
    g_bHeapNoiseInit = true;
}

// ============================================================================
// AntiTimingDelay — burns CPU time to mimic real VGC scheduling
// ============================================================================

static void AntiTimingDelay(LARGE_INTEGER& freq) {
    DWORD tid = GetCurrentThreadId();
    uint64_t minDelay = (tid % 3) + 2;  // 2-4ms

    LARGE_INTEGER start, now;
    QueryPerformanceCounter(&start);

    for (;;) {
        QueryPerformanceCounter(&now);
        uint64_t elapsedMs = 1000ULL * (now.QuadPart - start.QuadPart) / freq.QuadPart;
        if (elapsedMs >= minDelay) break;
        if (elapsedMs == 5 * (elapsedMs / 5))
            SwitchToThread();
    }

    // Random _mm_pause loop
    int pauseCount = rand() % 1000;
    for (int j = 0; j < pauseCount; j++)
        _mm_pause();
}

// ============================================================================
// CollectSystemInfo — periodic system info queries (anti-analysis fingerprint)
// ============================================================================

static void CollectSystemInfo() {
    ULONGLONG now = GetTickCount64();
    if (now - g_lastSysInfoTick <= IDLE_TIMEOUT_MS) return;

    SYSTEM_INFO si;
    GetSystemInfo(&si);

    MEMORYSTATUSEX ms;
    ms.dwLength = sizeof(ms);
    GlobalMemoryStatusEx(&ms);

    LARGE_INTEGER pf, pc;
    QueryPerformanceFrequency(&pf);
    QueryPerformanceCounter(&pc);

    g_lastSysInfoTick = now;
}

// ============================================================================
// PipeServerLoop — main named pipe emulation thread
// ============================================================================

static unsigned __stdcall PipeServerThread(void* /*arg*/) {
    DWORD errorCount = 0;
    DWORD packetCount = 0;

    while (!g_bStopping) {
        // --- Create security descriptor ---
        PSECURITY_DESCRIPTOR pSD = nullptr;
        SECURITY_ATTRIBUTES sa = {};

        if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(
                PIPE_DACL, SDDL_REVISION_1, &pSD, nullptr)) {
            Sleep(1000);
            continue;
        }

        sa.nLength = sizeof(sa);
        sa.lpSecurityDescriptor = pSD;
        sa.bInheritHandle = FALSE;

        // --- Create named pipe ---
        HANDLE hPipe = CreateNamedPipeW(
            PIPE_NAME,
            PIPE_OPEN_MODE,
            PIPE_MODE_FLAGS,
            PIPE_MAX_INST,
            PIPE_BUF_SIZE,
            PIPE_BUF_SIZE,
            0,
            &sa);

        LocalFree(pSD);

        if (hPipe == INVALID_HANDLE_VALUE) {
            Sleep(1000);
            continue;
        }

        // --- Overlapped connect ---
        OVERLAPPED ov = {};
        ov.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        if (!ov.hEvent) {
            CloseHandle(hPipe);
            Sleep(1000);
            continue;
        }

        BOOL connected = ConnectNamedPipe(hPipe, &ov);
        if (!connected) {
            DWORD err = GetLastError();
            if (err == ERROR_IO_PENDING) {
                HANDLE waits[2] = { ov.hEvent, g_hStopEvent };
                DWORD waitResult = WaitForMultipleObjects(2, waits, FALSE, INFINITE);
                if (waitResult != WAIT_OBJECT_0) {
                    // Stop event signaled or error
                    CancelIo(hPipe);
                    CloseHandle(ov.hEvent);
                    CloseHandle(hPipe);
                    break;
                }
            } else if (err != ERROR_PIPE_CONNECTED) {
                CloseHandle(ov.hEvent);
                CloseHandle(hPipe);
                Sleep(1000);
                continue;
            }
        }
        CloseHandle(ov.hEvent);

        // --- Generate heap noise on first connection ---
        GenerateHeapNoise();

        // --- Setup timing ---
        LARGE_INTEGER freq;
        QueryPerformanceFrequency(&freq);

        HANDLE hThread = GetCurrentThread();
        SetThreadPriority(hThread, THREAD_PRIORITY_NORMAL);

        // TLS session key
        DWORD tlsSlot = TlsAlloc();
        if (tlsSlot != TLS_OUT_OF_INDEXES) {
            void* sessionBuf = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, 0x100);
            if (sessionBuf) {
                ((uint32_t*)sessionBuf)[4] = TLS_MAGIC;
                TlsSetValue(tlsSlot, sessionBuf);
            }
        }

        ULONGLONG lastSuccessTick = GetTickCount64();

        LARGE_INTEGER sessionStart;
        QueryPerformanceCounter(&sessionStart);

        errorCount = 0;
        packetCount = 0;

        // --- Main read/write loop ---
        while (!g_bStopping) {
            // Anti-timing delay
            AntiTimingDelay(freq);

            // Read packet
            uint8_t buffer[PIPE_BUF_SIZE];
            DWORD bytesRead = 0;

            if (!ReadFile(hPipe, buffer, PIPE_BUF_SIZE, &bytesRead, nullptr) || !bytesRead) {
                // Handle read failure
                errorCount++;
                DWORD readErr = GetLastError();
                DWORD maxRetries = (readErr == ERROR_BROKEN_PIPE) ? BROKEN_PIPE_MAX : OTHER_ERROR_MAX;

                if (errorCount > maxRetries) break;

                DWORD tid = GetCurrentThreadId();
                Sleep(tid % 25 + 15);  // 15-39ms jitter

                if (GetTickCount64() - lastSuccessTick > IDLE_TIMEOUT_MS)
                    break;

                // Periodic system info collection
                if (packetCount % 100 == 0)
                    CollectSystemInfo();

                continue;
            }

            // Successful read
            errorCount = 0;
            lastSuccessTick = GetTickCount64();
            packetCount++;

            DWORD writeSize = bytesRead;

            // --- HANDSHAKE handling ---
            if (bytesRead >= 0x30 &&
                *(uint32_t*)buffer == VGC_MAGIC &&
                *(uint32_t*)(buffer + 4) == 16) {
                if (ValidatePacket(buffer, bytesRead)) {
                    BuildResponse(hPipe, buffer);
                    continue;  // response already written
                }
            }

            // --- PID injection ---
            if (bytesRead >= 8 && (*(uint32_t*)buffer & 0xF0000000) == 0xA0000000) {
                *(uint32_t*)(buffer + 4) = GetCurrentProcessId();
                if (bytesRead >= 0x10) {
                    FILETIME ftNow;
                    GetSystemTimeAsFileTime(&ftNow);
                    memcpy(buffer + 8, &ftNow, sizeof(FILETIME));
                }
            }

            // --- Periodic VGC signature append ---
            if (packetCount % APPEND_INTERVAL == 0 && writeSize + 4 <= PIPE_BUF_SIZE) {
                *(uint32_t*)(buffer + writeSize) = VGC_MAGIC;
                writeSize += 4;
            }

            // --- Write response ---
            DWORD bytesWritten = 0;
            if (!WriteFile(hPipe, buffer, writeSize, &bytesWritten, nullptr)) {
                Sleep(1);
                WriteFile(hPipe, buffer, writeSize, &bytesWritten, nullptr);
            }

            // --- Session timeout check ---
            DWORD sessionLimit = GetCurrentProcessId() % 0x5DC + 3500;
            if (packetCount > sessionLimit)
                break;

            // --- Max session time check (28 minutes) ---
            LARGE_INTEGER now64;
            QueryPerformanceCounter(&now64);
            int64_t elapsedNs;
            if (freq.QuadPart == 10000000LL) {
                elapsedNs = 100LL * (now64.QuadPart - sessionStart.QuadPart);
            } else {
                int64_t diff = now64.QuadPart - sessionStart.QuadPart;
                elapsedNs = 1000000000LL * (diff / freq.QuadPart)
                          + 1000000000LL * (diff % freq.QuadPart) / freq.QuadPart;
            }
            if (elapsedNs / 60000000000LL > MAX_SESSION_MIN)
                break;

            // Periodic maintenance
            if (packetCount % 100 == 0)
                CollectSystemInfo();
        }

        // --- Cleanup TLS ---
        if (tlsSlot != TLS_OUT_OF_INDEXES) {
            void* val = TlsGetValue(tlsSlot);
            if (val)
                HeapFree(GetProcessHeap(), 0, val);
            TlsFree(tlsSlot);
        }

        CloseHandle(hPipe);

        // If not stopping, reconnect
        if (g_bStopped) break;
    }

    return 0;
}

// ============================================================================
// Service control handler
// ============================================================================

static void WINAPI ServiceCtrlHandler(DWORD ctrlCode) {
    switch (ctrlCode) {
        case SERVICE_CONTROL_STOP:
        case SERVICE_CONTROL_SHUTDOWN:
            g_bStopping = true;
            if (g_hStopEvent)
                SetEvent(g_hStopEvent);
            g_ServiceStatus.dwCurrentState = SERVICE_STOP_PENDING;
            SetServiceStatus(g_hServiceStatus, &g_ServiceStatus);
            break;
        default:
            break;
    }
}

// ============================================================================
// ServiceMain
// ============================================================================

static void WINAPI ServiceMain(DWORD argc, LPWSTR* argv) {
    g_hServiceStatus = RegisterServiceCtrlHandlerW(SERVICE_NAME, ServiceCtrlHandler);
    if (!g_hServiceStatus) return;

    g_ServiceStatus.dwServiceType      = SERVICE_WIN32_OWN_PROCESS;
    g_ServiceStatus.dwControlsAccepted = SERVICE_ACCEPT_STOP;
    g_ServiceStatus.dwCurrentState     = SERVICE_START_PENDING;
    g_ServiceStatus.dwWin32ExitCode    = 0;
    g_ServiceStatus.dwCheckPoint       = 0;
    g_ServiceStatus.dwWaitHint         = 0;
    SetServiceStatus(g_hServiceStatus, &g_ServiceStatus);

    g_hStopEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!g_hStopEvent) {
        g_ServiceStatus.dwCurrentState = SERVICE_STOPPED;
        SetServiceStatus(g_hServiceStatus, &g_ServiceStatus);
        return;
    }

    g_ServiceStatus.dwCurrentState = SERVICE_RUNNING;
    SetServiceStatus(g_hServiceStatus, &g_ServiceStatus);

    g_bStopping = false;
    g_bStopped  = false;

    // Spawn pipe server thread
    HANDLE hThread = (HANDLE)_beginthreadex(nullptr, 0, PipeServerThread, nullptr, 0, nullptr);
    if (!hThread) {
        g_ServiceStatus.dwCurrentState = SERVICE_STOPPED;
        SetServiceStatus(g_hServiceStatus, &g_ServiceStatus);
        return;
    }

    // Wait for stop event
    while (WaitForSingleObject(g_hStopEvent, 100) == WAIT_TIMEOUT) {
        if (g_bStopped) break;
    }

    g_bStopping = true;
    WaitForSingleObject(hThread, 5000);
    CloseHandle(hThread);
    CloseHandle(g_hStopEvent);
    g_hStopEvent = nullptr;

    g_ServiceStatus.dwCurrentState = SERVICE_STOPPED;
    g_ServiceStatus.dwServiceSpecificExitCode = 0;
    SetServiceStatus(g_hServiceStatus, &g_ServiceStatus);
}

// ============================================================================
// Entry point
// ============================================================================

int wmain(int argc, wchar_t* argv[]) {
    SERVICE_TABLE_ENTRYW serviceTable[] = {
        { (LPWSTR)SERVICE_NAME, ServiceMain },
        { nullptr, nullptr }
    };

    if (!StartServiceCtrlDispatcherW(serviceTable)) {
        // If not started as a service, run pipe server directly (debug mode)
        g_hStopEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        PipeServerThread(nullptr);
    }

    return 0;
}
