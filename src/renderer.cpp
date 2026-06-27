// renderer.cpp — D3D11 + Dear ImGui host on Cheat Engine's main GUI thread.
//
// Two window modes:
//   * Normal   — a regular resizable window (great on a second monitor).
//   * Overlay  — a transparent, topmost, click-through window composited over
//                the screen with DirectComposition, so the UI floats over a
//                borderless-fullscreen game. No injection, no anti-tamper games:
//                it's just our own window painted on top.
//
// Everything runs on CE's main thread (driven by a CE Lua timer), so OnRender
// callbacks can call CE memory APIs directly.

#include "ce_imgui.h"

#include "imgui.h"
#include "backends/imgui_impl_win32.h"
#include "backends/imgui_impl_dx11.h"

#include <d3d11.h>
#include <dxgi1_3.h>
#include <dcomp.h>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "dcomp.lib")

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND, UINT, WPARAM, LPARAM);

namespace CEImGui {

static HWND                    g_hwnd      = nullptr;
static WNDCLASSEXW             g_wc        = {};
static ID3D11Device*           g_device    = nullptr;
static ID3D11DeviceContext*    g_context   = nullptr;
static IDXGISwapChain1*        g_swap      = nullptr;
static ID3D11RenderTargetView* g_rtv       = nullptr;

// overlay-only
static bool                    g_overlay   = false;
static IDCompositionDevice*    g_dcompDev  = nullptr;
static IDCompositionTarget*    g_dcompTgt  = nullptr;
static IDCompositionVisual*    g_dcompVis  = nullptr;
static bool                    g_interactive = true;   // is the overlay capturing input?
static int                     g_ovX=0, g_ovY=0, g_ovW=0, g_ovH=0;

static const wchar_t* kClassName = L"CEImGui_Host_Window";

// ---------------------------------------------------------------------------
static void CreateRTV() {
    ID3D11Texture2D* back = nullptr;
    if (SUCCEEDED(g_swap->GetBuffer(0, IID_PPV_ARGS(&back))) && back) {
        g_device->CreateRenderTargetView(back, nullptr, &g_rtv);
        back->Release();
    }
}
static void ReleaseRTV() { if (g_rtv) { g_rtv->Release(); g_rtv = nullptr; } }

// ---------------------------------------------------------------------------
static LRESULT WINAPI WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (ImGui_ImplWin32_WndProcHandler(hwnd, msg, wParam, lParam))
        return true;
    switch (msg) {
    case WM_SIZE:
        if (g_device && wParam != SIZE_MINIMIZED && g_swap && !g_overlay) {
            ReleaseRTV();
            g_swap->ResizeBuffers(0, (UINT)LOWORD(lParam), (UINT)HIWORD(lParam),
                                  DXGI_FORMAT_UNKNOWN, 0);
            CreateRTV();
        }
        return 0;
    case WM_DPICHANGED:
        if (!g_overlay) {
            UINT dpi = HIWORD(wParam);
            RendererSetScale((float)dpi / 96.0f);
            RECT* r = (RECT*)lParam;
            SetWindowPos(hwnd, nullptr, r->left, r->top, r->right-r->left, r->bottom-r->top,
                         SWP_NOZORDER | SWP_NOACTIVATE);
        }
        return 0;
    case WM_CLOSE:
        ShowWindow(hwnd, SW_HIDE);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

// ---------------------------------------------------------------------------
static void LoadFontsForScale(float scale) {
    ImGuiIO& io = ImGui::GetIO();
    io.Fonts->Clear();
    float px = 17.0f * scale;
    const char* segoe = "C:\\Windows\\Fonts\\segoeui.ttf";
    if (GetFileAttributesA(segoe) != INVALID_FILE_ATTRIBUTES)
        io.Fonts->AddFontFromFileTTF(segoe, px);
    else
        io.Fonts->AddFontDefault();
    io.Fonts->Build();
    ImGui_ImplDX11_InvalidateDeviceObjects();
}

void RendererSetScale(float scale) {
    if (scale < 0.5f) scale = 0.5f;
    if (scale > 4.0f) scale = 4.0f;
    State().dpiScale = scale;
    if (!State().rendererReady) return;
    ImGuiStyle& style = ImGui::GetStyle();
    ImGuiStyle base; ImGui::StyleColorsDark(&base);
    style = base;
    style.ScaleAllSizes(scale);
    LoadFontsForScale(scale);
}

// ---------------------------------------------------------------------------
static bool InitImGuiCommon(float scale) {
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    if (!g_overlay) io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    io.IniFilename = nullptr;
    ImGui::StyleColorsDark();
    ImGui::GetStyle().ScaleAllSizes(scale);
    ImGui_ImplWin32_Init(g_hwnd);
    ImGui_ImplDX11_Init(g_device, g_context);
    LoadFontsForScale(scale);
    return true;
}

// ---------------------------------------------------------------------------
static bool InitNormal() {
    g_wc = {};
    g_wc.cbSize = sizeof(WNDCLASSEXW);
    g_wc.style = CS_HREDRAW | CS_VREDRAW;
    g_wc.lpfnWndProc = WndProc;
    g_wc.hInstance = GetModuleHandleW(nullptr);
    g_wc.lpszClassName = kClassName;
    g_wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    RegisterClassExW(&g_wc);

    g_hwnd = CreateWindowExW(0, kClassName, L"CE-ImGui", WS_OVERLAPPEDWINDOW,
                             CW_USEDEFAULT, CW_USEDEFAULT, 1100, 720,
                             nullptr, nullptr, g_wc.hInstance, nullptr);
    if (!g_hwnd) return false;

    UINT dpi = GetDpiForWindow(g_hwnd);
    float scale = dpi ? (float)dpi / 96.0f : 1.0f;
    State().dpiScale = scale;

    DXGI_SWAP_CHAIN_DESC sd = {};
    sd.BufferCount = 2;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = g_hwnd;
    sd.SampleDesc.Count = 1;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
    D3D_FEATURE_LEVEL fl;
    const D3D_FEATURE_LEVEL levels[] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0 };
    IDXGISwapChain* sc0 = nullptr;
    if (FAILED(D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
            0, levels, 2, D3D11_SDK_VERSION, &sd, &sc0, &g_device, &fl, &g_context)))
        return false;
    sc0->QueryInterface(IID_PPV_ARGS(&g_swap));
    sc0->Release();
    CreateRTV();
    if (!g_rtv) return false;

    ShowWindow(g_hwnd, SW_SHOW);
    UpdateWindow(g_hwnd);
    return InitImGuiCommon(scale);
}

// ---------------------------------------------------------------------------
static bool InitOverlay() {
    // Cover the primary monitor.
    POINT o = {0,0};
    HMONITOR mon = MonitorFromPoint(o, MONITOR_DEFAULTTOPRIMARY);
    MONITORINFO mi = { sizeof(mi) };
    GetMonitorInfo(mon, &mi);
    g_ovX = mi.rcMonitor.left;  g_ovY = mi.rcMonitor.top;
    g_ovW = mi.rcMonitor.right - mi.rcMonitor.left;
    g_ovH = mi.rcMonitor.bottom - mi.rcMonitor.top;

    g_wc = {};
    g_wc.cbSize = sizeof(WNDCLASSEXW);
    g_wc.style = CS_HREDRAW | CS_VREDRAW;
    g_wc.lpfnWndProc = WndProc;
    g_wc.hInstance = GetModuleHandleW(nullptr);
    g_wc.lpszClassName = kClassName;
    g_wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    RegisterClassExW(&g_wc);

    // Topmost, no taskbar entry. WS_EX_TRANSPARENT (click-through) is toggled
    // at runtime based on whether a form is visible.
    DWORD ex = WS_EX_TOPMOST | WS_EX_TOOLWINDOW;
    g_hwnd = CreateWindowExW(ex, kClassName, L"CE-ImGui Overlay", WS_POPUP,
                             g_ovX, g_ovY, g_ovW, g_ovH,
                             nullptr, nullptr, g_wc.hInstance, nullptr);
    if (!g_hwnd) return false;

    UINT dpi = GetDpiForWindow(g_hwnd);
    float scale = dpi ? (float)dpi / 96.0f : 1.0f;
    State().dpiScale = scale;

    // D3D11 device (BGRA support required for DirectComposition).
    D3D_FEATURE_LEVEL fl;
    const D3D_FEATURE_LEVEL levels[] = { D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0 };
    if (FAILED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
            D3D11_CREATE_DEVICE_BGRA_SUPPORT, levels, 2, D3D11_SDK_VERSION,
            &g_device, &fl, &g_context)))
        return false;

    IDXGIDevice* dxgiDev = nullptr;
    if (FAILED(g_device->QueryInterface(IID_PPV_ARGS(&dxgiDev)))) return false;
    IDXGIFactory2* factory = nullptr;
    if (FAILED(CreateDXGIFactory2(0, IID_PPV_ARGS(&factory)))) { dxgiDev->Release(); return false; }

    DXGI_SWAP_CHAIN_DESC1 sd = {};
    sd.Width  = g_ovW;
    sd.Height = g_ovH;
    sd.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    sd.SampleDesc.Count = 1;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.BufferCount = 2;
    sd.Scaling = DXGI_SCALING_STRETCH;
    sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
    sd.AlphaMode = DXGI_ALPHA_MODE_PREMULTIPLIED;   // per-pixel transparency
    if (FAILED(factory->CreateSwapChainForComposition(g_device, &sd, nullptr, &g_swap))) {
        factory->Release(); dxgiDev->Release(); return false;
    }
    CreateRTV();

    // DirectComposition: bind the swapchain as the window's content.
    if (FAILED(DCompositionCreateDevice(dxgiDev, IID_PPV_ARGS(&g_dcompDev)))) {
        factory->Release(); dxgiDev->Release(); return false;
    }
    g_dcompDev->CreateTargetForHwnd(g_hwnd, TRUE, &g_dcompTgt);
    g_dcompDev->CreateVisual(&g_dcompVis);
    g_dcompVis->SetContent(g_swap);
    g_dcompTgt->SetRoot(g_dcompVis);
    g_dcompDev->Commit();
    factory->Release(); dxgiDev->Release();

    if (!g_rtv) return false;

    ShowWindow(g_hwnd, SW_SHOWNOACTIVATE);
    return InitImGuiCommon(scale);
}

// ---------------------------------------------------------------------------
bool RendererInit() {
    if (State().rendererReady) return true;
    g_overlay = State().overlayMode;

    DPI_AWARENESS_CONTEXT prev =
        SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    bool ok = g_overlay ? InitOverlay() : InitNormal();

    if (prev) SetThreadDpiAwarenessContext(prev);
    if (ok) State().rendererReady = true;
    return ok;
}

// ---------------------------------------------------------------------------
static void SetClickThrough(bool through) {
    LONG_PTR ex = GetWindowLongPtrW(g_hwnd, GWL_EXSTYLE);
    if (through) ex |= WS_EX_TRANSPARENT;
    else         ex &= ~WS_EX_TRANSPARENT;
    SetWindowLongPtrW(g_hwnd, GWL_EXSTYLE, ex);
    if (!through) SetForegroundWindow(g_hwnd);
}

// ---------------------------------------------------------------------------
void RendererFrame() {
    if (!State().rendererReady) {
        if (!State().wantRenderer) return;
        if (!RendererInit()) return;
    }
    if (!g_rtv) return;

    // In overlay mode, capture input only while a form is visible; otherwise
    // be click-through so the game receives input.
    if (g_overlay) {
        bool wantInteractive = VisibleFormCount() > 0;
        if (wantInteractive != g_interactive) {
            SetClickThrough(!wantInteractive);
            g_interactive = wantInteractive;
        }
        ImGui::GetIO().MouseDrawCursor = wantInteractive;
    }

    ImGui_ImplDX11_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();
    RenderForms();
    ImGui::Render();

    const float clearNormal[4]  = { 0.07f, 0.07f, 0.09f, 1.0f };
    const float clearOverlay[4] = { 0.0f, 0.0f, 0.0f, 0.0f };   // fully transparent
    g_context->OMSetRenderTargets(1, &g_rtv, nullptr);
    g_context->ClearRenderTargetView(g_rtv, g_overlay ? clearOverlay : clearNormal);
    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());

    g_swap->Present(1, 0);
}

// ---------------------------------------------------------------------------
void RendererShutdown() {
    if (State().rendererReady) {
        ImGui_ImplDX11_Shutdown();
        ImGui_ImplWin32_Shutdown();
        ImGui::DestroyContext();
        State().rendererReady = false;
    }
    if (g_dcompVis) { g_dcompVis->Release(); g_dcompVis = nullptr; }
    if (g_dcompTgt) { g_dcompTgt->Release(); g_dcompTgt = nullptr; }
    if (g_dcompDev) { g_dcompDev->Release(); g_dcompDev = nullptr; }
    ReleaseRTV();
    if (g_swap)    { g_swap->Release();    g_swap = nullptr; }
    if (g_context) { g_context->Release(); g_context = nullptr; }
    if (g_device)  { g_device->Release();  g_device = nullptr; }
    if (g_hwnd)    { DestroyWindow(g_hwnd); g_hwnd = nullptr; }
    UnregisterClassW(kClassName, g_wc.hInstance);
}

} // namespace CEImGui
