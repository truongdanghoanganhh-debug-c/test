#pragma once
/*
 * spoofcall.h — Return Address Spoofing for Game Function Calls
 *
 * When calling engine functions (GetBoneMatrix, etc), the return address
 * on the stack points into our DLL. Anti-cheat scans call stacks for
 * non-game return addresses. This system spoofs by routing through a
 * gadget found in the game's own executable sections.
 *
 * The assembly stub (spoofcall.asm) handles the actual stack manipulation.
 * Magic sentinel: 0x52A3450 (MagicOffsets from offsets.h)
 */

#include <Windows.h>
#include <cstdint>
#include <vector>
#include <random>

// ─── Assembly exports ─────────────────────────────────────────────────
extern "C" {
    void spoofcall_stub();
    extern uintptr_t proxy_call_returns[32];
    extern uintptr_t proxy_call_fakestack;
    extern uintptr_t proxy_call_fakestack_size;
}

namespace SpoofCall {

    // Gadget: FF 15 xx xx xx xx 48 83 C4 xx C3
    // = call [rip+x]; add rsp, N; ret
    struct Gadget {
        uintptr_t addr;
        uint8_t   stackCleanup;
    };

    inline std::vector<Gadget>   g_Gadgets;
    inline std::vector<uintptr_t> g_FakeStack;
    inline bool                  g_Initialized = false;

    // Scan game module's .text sections for call-gadgets
    inline bool Initialize(uintptr_t moduleBase) {
        if (g_Initialized) return true;

        auto* dos = (IMAGE_DOS_HEADER*)moduleBase;
        auto* nt  = (IMAGE_NT_HEADERS*)((uint8_t*)moduleBase + dos->e_lfanew);
        auto* sec = IMAGE_FIRST_SECTION(nt);

        for (int i = 0; i < nt->FileHeader.NumberOfSections; i++, sec++) {
            // Only scan executable sections
            if (!(sec->Characteristics & IMAGE_SCN_MEM_EXECUTE))
                continue;

            uintptr_t start = moduleBase + sec->VirtualAddress;
            DWORD     size  = sec->Misc.VirtualSize;
            if (size < 12) continue;

            __try {
                auto* data = (const uint8_t*)start;
                for (DWORD j = 0; j + 11 < size; j++) {
                    // Pattern: FF 15 ?? ?? ?? ?? 48 83 C4 ?? C3
                    if (data[j]   == 0xFF && data[j+1] == 0x15 &&
                        data[j+6] == 0x48 && data[j+7] == 0x83 &&
                        data[j+8] == 0xC4 && data[j+10] == 0xC3) {
                        Gadget g;
                        g.addr = start + j;
                        g.stackCleanup = data[j+9];
                        g_Gadgets.push_back(g);

                        if (g_Gadgets.size() >= 500)
                            break; // enough gadgets
                    }
                }
            } __except(1) {
                continue;
            }

            if (g_Gadgets.size() >= 500)
                break;
        }

        if (g_Gadgets.empty())
            return false;

        // Build fake callstack with random addresses inside game module
        std::mt19937_64 rng(GetTickCount64());
        DWORD imgSize = nt->OptionalHeader.SizeOfImage;

        g_FakeStack.resize(16);
        for (auto& addr : g_FakeStack) {
            // Use random gadget addresses mixed with random game module offsets
            if (rng() % 2 == 0 && !g_Gadgets.empty()) {
                addr = g_Gadgets[rng() % g_Gadgets.size()].addr;
            } else {
                addr = moduleBase + (rng() % imgSize);
            }
        }

        proxy_call_fakestack      = (uintptr_t)g_FakeStack.data();
        proxy_call_fakestack_size = g_FakeStack.size();

        // Pre-populate return address slots with gadgets
        for (int i = 0; i < 32 && !g_Gadgets.empty(); i++) {
            proxy_call_returns[i] = g_Gadgets[rng() % g_Gadgets.size()].addr;
        }

        g_Initialized = true;
        return true;
    }

    // Find a gadget with a specific stack cleanup size
    inline uintptr_t FindGadget(uint8_t cleanupSize) {
        for (auto& g : g_Gadgets) {
            if (g.stackCleanup == cleanupSize)
                return g.addr;
        }
        // Fallback: return any gadget
        return g_Gadgets.empty() ? 0 : g_Gadgets[0].addr;
    }

    // Template wrapper for spoofed calls
    // Appends magic sentinel + function pointer as last args
    template<typename Ret, typename... Args>
    inline Ret Call(void* func, Args... args) {
        if (!g_Initialized || g_Gadgets.empty()) {
            // Fallback: direct call without spoofing
            auto fn = reinterpret_cast<Ret(__fastcall*)(Args...)>(func);
            return fn(args...);
        }
        constexpr uintptr_t MAGIC = 0x52A3450;
        auto caller = reinterpret_cast<Ret(__fastcall*)(Args..., uintptr_t, void*)>(spoofcall_stub);
        return caller(args..., MAGIC, func);
    }
}
