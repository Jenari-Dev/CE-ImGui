// renderer.cpp — D3D11 + Dear ImGui host, all on CE's main GUI thread.
//
// We do NOT spin our own thread or message loop. The window is created on the
// main thread during a timer tick, so Cheat Engine's own message pump dispatches
// its messages. We just render one frame per tick.

#include "ce_imgui.h"

#include "imgui.h"
#include "backends/imgui_impl_win32.h"
#include "backends/imgui_impl_dx11.h"

#include <d3d11.h>
#include <string>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "dwmapi.lib")

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND, UINT, WPARAM, LPARAM);

namespace CEImGui {

// ---- D3D11 / window globals (single-threaded, so plain statics are fine) ----
static HWND                    g_hwnd      = nullptr;
static WNDCLASSEXW             g_wc        = {};
static ID3D11Device*           g_device    = nullptr;
static ID3D11DeviceContext*    g_context   = nullptr;
static IDXGISwapChain*         g_swap      = nullptr;
static ID3D11RenderTargetView* g_rtv       = nullptr;
static bool                    g_dpiInit   = false;

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

static bool CreateDeviceD3D(HWND hwnd) {
    DXGI_SWAP_CHAIN_DESC sd = {};
    sd.BufferCount       = 2;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferDesc.RefreshRate.Numerator   = 60;
    sd.BufferDesc.RefreshRate.Denominator = 1;
    sd.BufferUsage  = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = hwnd;
    sd.SampleDesc.Count = 1;
    sd.Windowed     = TRUE;
    sd.SwapEffect   = DXGI_SWAP_EFFECT_DISCARD;

    UINT flags = 0;
    D3D_FEATURE_LEVEL fl;
    const D3D_FEATURE_LEVEL levels[] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0 };
    HRESULT hr = D3D11CreateDeviceAndSwapChain(
        nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags, levels, 2,
        D3D11_SDK_VERSION, &sd, &g_swap, &g_device, &fl, &g_context);
    if (FAILED(hr)) return false;
    CreateRTV();
    return g_rtv != nullptr;
}

static void CleanupDeviceD3D() {
    ReleaseRTV();
    if (g_swap)    { g_swap->Release();    g_swap = nullptr; }
    if (g_context) { g_context->Release(); g_context = nullptr; }
    if (g_device)  { g_device->Release();  g_device = nullptr; }
}

// ---------------------------------------------------------------------------
static LRESULT WINAPI WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (ImGui_ImplWin32_WndProcHandler(hwnd, msg, wParam, lParam))
        return true;

    switch (msg) {
    case WM_SIZE:
        if (g_device && wParam != SIZE_MINIMIZED && g_swap) {
            ReleaseRTV();
            g_swap->ResizeBuffers(0, (UINT)LOWORD(lParam), (UINT)HIWORD(lParam),
                                  DXGI_FORMAT_UNKNOWN, 0);
            CreateRTV();
        }
        return 0;
    case WM_DPICHANGED: {
        // Window moved to a monitor with different DPI — rescale UI.
        UINT dpi = HIWORD(wParam);
        RendererSetScale((float)dpi / 96.0f);
        RECT* r = (RECT*)lParam;
        SetWindowPos(hwnd, nullptr, r->left, r->top,
                     r->right - r->left, r->bottom - r->top,
                     SWP_NOZORDER | SWP_NOACTIVATE);
        return 0;
    }
    case WM_CLOSE:
        // Don't destroy — just hide. Lua still owns it.
        ShowWindow(hwnd, SW_HIDE);
        return 0;
    case WM_DESTROY:
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

// ---------------------------------------------------------------------------
static void LoadFontsForScale(float scale) {
    ImGuiIO& io = ImGui::GetIO();
    io.Fonts->Clear();
    float px = 17.0f * scale;
    // Prefer a crisp system font; fall back to the built-in default.
    const char* segoe = "C:\\Windows\\Fonts\\segoeui.ttf";
    if (GetFileAttributesA(segoe) != INVALID_FILE_ATTRIBUTES)
        io.Fonts->AddFontFromFileTTF(segoe, px);
    else
        io.Fonts->AddFontDefault();
    io.Fonts->Build();
    ImGui_ImplDX11_InvalidateDeviceObjects(); // rebuild font texture
}

void RendererSetScale(float scale) {
    if (scale < 0.5f) scale = 0.5f;
    if (scale > 4.0f) scale = 4.0f;
    State().dpiScale = scale;
    if (!State().rendererReady) return;

    // Reset style to defaults, then scale everything for the new DPI.
    ImGuiStyle& style = ImGui::GetStyle();
    ImGuiStyle base; ImGui::StyleColorsDark(&base);
    style = base;
    style.ScaleAllSizes(scale);
    LoadFontsForScale(scale);
}

// ---------------------------------------------------------------------------
bool RendererInit() {
    if (State().rendererReady) return true;

    // Opt THIS thread/window into per-monitor DPI awareness so we render at
    // native 4K resolution and stay crisp — without changing CE's own awareness.
    DPI_AWARENESS_CONTEXT prev =
        SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    g_wc = {};
    g_wc.cbSize        = sizeof(WNDCLASSEXW);
    g_wc.style         = CS_HREDRAW | CS_VREDRAW;
    g_wc.lpfnWndProc   = WndProc;
    g_wc.hInstance     = GetModuleHandleW(nullptr);
    g_wc.lpszClassName = kClassName;
    g_wc.hCursor       = LoadCursor(nullptr, IDC_ARROW);
    RegisterClassExW(&g_wc);

    g_hwnd = CreateWindowExW(0, kClassName, L"CE-ImGui",
                             WS_OVERLAPPEDWINDOW,
                             CW_USEDEFAULT, CW_USEDEFAULT, 1100, 720,
                             nullptr, nullptr, g_wc.hInstance, nullptr);
    if (!g_hwnd) {
        if (prev) SetThreadDpiAwarenessContext(prev);
        return false;
    }

    // Detect DPI of the monitor the window landed on.
    UINT dpi = GetDpiForWindow(g_hwnd);
    float scale = dpi ? (float)dpi / 96.0f : 1.0f;
    State().dpiScale = scale;

    if (!CreateDeviceD3D(g_hwnd)) {
        DestroyWindow(g_hwnd); g_hwnd = nullptr;
        UnregisterClassW(kClassName, g_wc.hInstance);
        if (prev) SetThreadDpiAwarenessContext(prev);
        return false;
    }

    ShowWindow(g_hwnd, SW_SHOW);
    UpdateWindow(g_hwnd);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    io.IniFilename = nullptr; // don't write imgui.ini into CE's folder

    ImGui::StyleColorsDark();
    ImGui::GetStyle().ScaleAllSizes(scale);

    ImGui_ImplWin32_Init(g_hwnd);
    ImGui_ImplDX11_Init(g_device, g_context);

    State().rendererReady = true;
    LoadFontsForScale(scale);   // load the scaled font now that DX11 is up

    if (prev) SetThreadDpiAwarenessContext(prev);
    g_dpiInit = true;
    return true;
}

// ---------------------------------------------------------------------------
void RendererFrame() {
    // Lazily create the window + D3D11 on THIS (main) thread the first time a
    // form exists — never on the thread that called CreateForm, so rendering
    // and device creation stay on the same thread.
    if (!State().rendererReady) {
        if (!State().wantRenderer) return;
        if (!RendererInit()) return;
    }
    if (!g_rtv) return;

    ImGui_ImplDX11_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();

    // Run every Lua form's OnRender callback (immediate mode happens here).
    RenderForms();

    ImGui::Render();
    const float clear[4] = { 0.07f, 0.07f, 0.09f, 1.0f };
    g_context->OMSetRenderTargets(1, &g_rtv, nullptr);
    g_context->ClearRenderTargetView(g_rtv, clear);
    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());

    g_swap->Present(1, 0); // vsync
}

// ---------------------------------------------------------------------------
void RendererShutdown() {
    if (State().rendererReady) {
        ImGui_ImplDX11_Shutdown();
        ImGui_ImplWin32_Shutdown();
        ImGui::DestroyContext();
        State().rendererReady = false;
    }
    CleanupDeviceD3D();
    if (g_hwnd) { DestroyWindow(g_hwnd); g_hwnd = nullptr; }
    UnregisterClassW(kClassName, g_wc.hInstance);
}

HWND RendererHwnd() { return g_hwnd; }

} // namespace CEImGui
