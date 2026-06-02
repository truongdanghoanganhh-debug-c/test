/*
 * hooks.cpp — DX11 VTable Hooking, ImGui Init, Game Loop
 *
 * Hooks IDXGISwapChain::Present (vtable[8]) and ResizeBuffers (vtable[13])
 * via MinHook. Initializes ImGui with DX11+Win32 backends. Runs the game
 * update and ESP draw loop every frame from the Present hook.
 */

#include <Windows.h>
#include <d3d11.h>
#include <dxgi.h>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")

#include <imgui.h>
#include <imgui_impl_dx11.h>
#include <imgui_impl_win32.h>
#include <MinHook.h>

#include "hooks.h"
#include "game.h"
#include "menu.h"

// ImGui WndProc handler (declared in imgui_impl_win32.cpp)
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

namespace Hooks {

    // ─── Globals ──────────────────────────────────────────────────────
    PresentFn  oPresent       = nullptr;
    ResizeFn   oResize        = nullptr;
    bool       g_Initialized  = false;
    HWND       g_GameWindow   = nullptr;

    static ID3D11Device*           g_Device         = nullptr;
    static ID3D11DeviceContext*    g_Context        = nullptr;
    static WNDPROC                 g_OrigWndProc    = nullptr;
    static bool                    g_WasMenuOpen    = false;

    // ─── WndProc Hook ─────────────────────────────────────────────────
    static LRESULT CALLBACK WndProcHook(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
        // Toggle menu on INSERT (handle on key-down only, prevent repeat)
        if (msg == WM_KEYDOWN && wParam == VK_INSERT && !(lParam & (1 << 30))) {
            Menu::bMenuOpen = !Menu::bMenuOpen;

            if (Menu::bMenuOpen) {
                // Menu just opened: show cursor, unclip it
                while (ShowCursor(TRUE) < 0) {}  // keep calling until visible
                ClipCursor(nullptr);               // free the cursor from game clip region

                // Set ImGui IO to capture mouse/keyboard
                ImGuiIO& io = ImGui::GetIO();
                io.MouseDrawCursor = true;
                io.WantCaptureMouse = true;
                io.WantCaptureKeyboard = true;
            } else {
                // Menu just closed: hide cursor, let game re-clip
                while (ShowCursor(FALSE) >= 0) {}  // hide until count < 0

                ImGuiIO& io = ImGui::GetIO();
                io.MouseDrawCursor = false;
                io.WantCaptureMouse = false;
                io.WantCaptureKeyboard = false;
            }
            return 0;
        }

        // Always forward to ImGui first (it needs to track mouse position etc.)
        if (g_Initialized && ImGui::GetCurrentContext()) {
            ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam);
        }

        // When menu is open: block ALL input from reaching the game
        if (Menu::bMenuOpen) {
            switch (msg) {
            // Mouse buttons
            case WM_LBUTTONDOWN: case WM_LBUTTONUP: case WM_LBUTTONDBLCLK:
            case WM_RBUTTONDOWN: case WM_RBUTTONUP: case WM_RBUTTONDBLCLK:
            case WM_MBUTTONDOWN: case WM_MBUTTONUP: case WM_MBUTTONDBLCLK:
            case WM_XBUTTONDOWN: case WM_XBUTTONUP: case WM_XBUTTONDBLCLK:
            // Mouse movement
            case WM_MOUSEMOVE:
            case WM_MOUSEWHEEL:  case WM_MOUSEHWHEEL:
            // Keyboard
            case WM_KEYDOWN:     case WM_KEYUP:
            case WM_SYSKEYDOWN:  case WM_SYSKEYUP:
            case WM_CHAR:        case WM_SYSCHAR:
            // Raw input (Valorant captures mouse through this!)
            case WM_INPUT:
            // Touch/pen
            case WM_TOUCH:       case WM_POINTERDOWN: case WM_POINTERUP:
            case WM_POINTERUPDATE:
                return 0;  // eat the message

            // Cursor management — keep cursor visible
            case WM_SETCURSOR:
                SetCursor(LoadCursorW(nullptr, MAKEINTRESOURCEW(32512)));
                return 1;  // we handled it

            // Prevent game from re-clipping cursor
            case WM_ACTIVATEAPP:
            case WM_ACTIVATE:
            case WM_SETFOCUS:
                ClipCursor(nullptr);
                return 0;
            }
        }

        return CallWindowProcW(g_OrigWndProc, hWnd, msg, wParam, lParam);
    }

    // ─── Present Hook ─────────────────────────────────────────────────
    static HRESULT __stdcall hkPresent(IDXGISwapChain* pSwapChain, UINT SyncInterval, UINT Flags) {
        if (!g_Initialized) {
            // First call — get device and init ImGui
            if (SUCCEEDED(pSwapChain->GetDevice(__uuidof(ID3D11Device), (void**)&g_Device))) {
                g_Device->GetImmediateContext(&g_Context);

                // Get the game window from the swap chain
                DXGI_SWAP_CHAIN_DESC desc;
                pSwapChain->GetDesc(&desc);
                g_GameWindow = desc.OutputWindow;

                Game::ScreenW = (float)desc.BufferDesc.Width;
                Game::ScreenH = (float)desc.BufferDesc.Height;

                // Init ImGui — do NOT set NoMouseCursorChange, we want ImGui to manage the cursor
                ImGui::CreateContext();
                ImGuiIO& io = ImGui::GetIO();
                io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
                io.MouseDrawCursor = false;  // start hidden, we toggle on INSERT

                Menu::ApplyTheme();

                ImGui_ImplWin32_Init(g_GameWindow);
                ImGui_ImplDX11_Init(g_Device, g_Context);

                // Hook WndProc
                g_OrigWndProc = (WNDPROC)SetWindowLongPtrW(g_GameWindow, GWLP_WNDPROC, (LONG_PTR)WndProcHook);

                // Release the device ref we got (it belongs to the game)
                g_Device->Release();

                g_Initialized = true;
            }
        }

        if (g_Initialized) {
            // ── Per-frame: create RTV, render, release ──
            ID3D11RenderTargetView* rtv = nullptr;
            ID3D11Texture2D* backBuffer = nullptr;

            if (SUCCEEDED(pSwapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&backBuffer))) {
                g_Device->CreateRenderTargetView(backBuffer, nullptr, &rtv);
                backBuffer->Release();
            }

            if (rtv) {
                // ImGui new frame
                ImGui_ImplDX11_NewFrame();
                ImGui_ImplWin32_NewFrame();
                ImGui::NewFrame();

                // Game updates
                Game::UpdatePointers();
                Game::UpdateCamera();

                // Draw ESP overlay
                Menu::DrawESP();

                // Per-frame cursor/input management
                ImGuiIO& io = ImGui::GetIO();
                if (Menu::bMenuOpen) {
                    // Force cursor visible + unclipped every frame
                    // (game tries to re-hide/re-clip each frame)
                    io.MouseDrawCursor = true;
                    io.WantCaptureMouse = true;
                    io.WantCaptureKeyboard = true;
                    ClipCursor(nullptr);

                    Menu::Draw();
                } else {
                    io.MouseDrawCursor = false;
                    io.WantCaptureMouse = false;
                    io.WantCaptureKeyboard = false;
                }

                // Render
                ImGui::Render();
                g_Context->OMSetRenderTargets(1, &rtv, nullptr);
                ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());

                // Release per-frame RTV (do NOT cache!)
                rtv->Release();
            }
        }

        return oPresent(pSwapChain, SyncInterval, Flags);
    }

    // ─── ResizeBuffers Hook ───────────────────────────────────────────
    static HRESULT __stdcall hkResize(IDXGISwapChain* pSwapChain, UINT BufferCount,
                                       UINT Width, UINT Height, DXGI_FORMAT Format, UINT Flags) {
        // Update screen dimensions
        if (Width > 0 && Height > 0) {
            Game::ScreenW = (float)Width;
            Game::ScreenH = (float)Height;
        }

        // Invalidate ImGui resources
        if (g_Initialized) {
            ImGui_ImplDX11_InvalidateDeviceObjects();
        }

        HRESULT hr = oResize(pSwapChain, BufferCount, Width, Height, Format, Flags);

        if (g_Initialized) {
            ImGui_ImplDX11_CreateDeviceObjects();
        }

        return hr;
    }

    // ─── Init: Create dummy device, hook vtable ───────────────────────
    bool Init() {
        // Initialize game module base
        if (!Game::Init()) {
            printf("[NullWare] Failed to find game module!\n");
            return false;
        }
        printf("[NullWare] Game base: 0x%llX\n", Game::Base);

        // Create a hidden window for dummy swap chain
        WNDCLASSEXW wc = {};
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = DefWindowProcW;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.lpszClassName = L"NW_Dummy";
        RegisterClassExW(&wc);

        HWND hDummy = CreateWindowExW(0, L"NW_Dummy", L"", WS_OVERLAPPED,
                                       0, 0, 100, 100, nullptr, nullptr, wc.hInstance, nullptr);

        // Create dummy D3D11 device + swap chain
        DXGI_SWAP_CHAIN_DESC sd = {};
        sd.BufferCount = 1;
        sd.BufferDesc.Width = 2;
        sd.BufferDesc.Height = 2;
        sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        sd.BufferDesc.RefreshRate.Numerator = 60;
        sd.BufferDesc.RefreshRate.Denominator = 1;
        sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        sd.OutputWindow = hDummy;
        sd.SampleDesc.Count = 1;
        sd.Windowed = TRUE;
        sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

        IDXGISwapChain* pDummySwap = nullptr;
        ID3D11Device*   pDummyDev  = nullptr;
        ID3D11DeviceContext* pDummyCtx = nullptr;

        D3D_FEATURE_LEVEL featureLevel;
        HRESULT hr = D3D11CreateDeviceAndSwapChain(
            nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
            0, nullptr, 0, D3D11_SDK_VERSION,
            &sd, &pDummySwap, &pDummyDev, &featureLevel, &pDummyCtx
        );

        if (FAILED(hr)) {
            DestroyWindow(hDummy);
            UnregisterClassW(L"NW_Dummy", wc.hInstance);
            printf("[NullWare] Failed to create dummy D3D11 device! HRESULT: 0x%08lX\n", hr);
            return false;
        }

        // Copy vtable pointers
        void** vtable = *(void***)pDummySwap;
        void* pPresentTarget = vtable[8];    // IDXGISwapChain::Present
        void* pResizeTarget  = vtable[13];   // IDXGISwapChain::ResizeBuffers

        // Cleanup dummy
        pDummyCtx->Release();
        pDummyDev->Release();
        pDummySwap->Release();
        DestroyWindow(hDummy);
        UnregisterClassW(L"NW_Dummy", wc.hInstance);

        // Hook with MinHook
        if (MH_Initialize() != MH_OK) {
            printf("[NullWare] MinHook init failed!\n");
            return false;
        }

        if (MH_CreateHook(pPresentTarget, &hkPresent, (void**)&oPresent) != MH_OK) {
            printf("[NullWare] Failed to hook Present!\n");
            return false;
        }

        if (MH_CreateHook(pResizeTarget, &hkResize, (void**)&oResize) != MH_OK) {
            printf("[NullWare] Failed to hook ResizeBuffers!\n");
            return false;
        }

        if (MH_EnableHook(MH_ALL_HOOKS) != MH_OK) {
            printf("[NullWare] Failed to enable hooks!\n");
            return false;
        }

        printf("[NullWare] Hooks installed successfully.\n");
        return true;
    }

    // ─── Shutdown ─────────────────────────────────────────────────────
    void Shutdown() {
        // 1. Mark as not initialized
        g_Initialized = false;

        // 2. Wait for in-flight Present to finish
        Sleep(500);

        // 3. Restore WndProc FIRST (before disabling hooks)
        if (g_OrigWndProc && g_GameWindow) {
            SetWindowLongPtrW(g_GameWindow, GWLP_WNDPROC, (LONG_PTR)g_OrigWndProc);
            g_OrigWndProc = nullptr;
        }

        // 4. Disable and remove hooks
        MH_DisableHook(MH_ALL_HOOKS);
        MH_RemoveHook(MH_ALL_HOOKS);
        MH_Uninitialize();

        // 5. Shutdown ImGui (DX11 → Win32 → Context)
        ImGui_ImplDX11_Shutdown();
        ImGui_ImplWin32_Shutdown();
        ImGui::DestroyContext();

        // 6. Do NOT release g_Device/g_Context — they belong to the game!
        g_Device  = nullptr;
        g_Context = nullptr;
    }
}
