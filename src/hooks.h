#pragma once
/*
 * hooks.h — DX11 Present/ResizeBuffers hooks, ImGui initialization, WndProc
 */

#include <d3d11.h>
#include <dxgi.h>

namespace Hooks {
    bool Init();
    void Shutdown();

    // Present hook — called every frame
    using PresentFn = HRESULT(__stdcall*)(IDXGISwapChain*, UINT, UINT);
    using ResizeFn  = HRESULT(__stdcall*)(IDXGISwapChain*, UINT, UINT, UINT, DXGI_FORMAT, UINT);

    extern PresentFn  oPresent;
    extern ResizeFn   oResize;
    extern bool       g_Initialized;
    extern HWND       g_GameWindow;
}
