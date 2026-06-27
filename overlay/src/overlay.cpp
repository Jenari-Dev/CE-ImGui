// overlay.cpp — injected in-game Dear ImGui overlay for DirectX 12 games.
//
// Proof of concept: inject this DLL into a DX12 game, it hooks the swapchain
// Present and draws an ImGui menu inside the game's frame. Toggle with INSERT.
//
// How the DX12 hook works:
//   1. Build a throwaway device + swapchain + command queue to read their
//      vtable pointers (the functions live in the system DLLs, shared by the
//      game's real objects).
//   2. MinHook Present / ResizeBuffers / ExecuteCommandLists.
//   3. ExecuteCommandLists hook captures the game's command queue (DX12 needs
//      it to submit our overlay's draw commands).
//   4. On first Present, set up ImGui's DX12 backend against the game's device
//      and render every frame.

#include <windows.h>
#include <d3d12.h>
#include <dxgi1_4.h>
#include <vector>

#include "imgui.h"
#include "backends/imgui_impl_win32.h"
#include "backends/imgui_impl_dx12.h"
#include "MinHook.h"

#include <cstdio>
#include <cstdarg>
// Diagnostic logging to a temp file we can read after a crash.
static void LOG(const char* fmt, ...) {
    char path[MAX_PATH]; GetTempPathA(MAX_PATH, path);
    strcat_s(path, "ceimgui_overlay.log");
    static bool first = true;
    FILE* f = nullptr; fopen_s(&f, path, first ? "w" : "a"); first = false;
    if (!f) return;
    va_list ap; va_start(ap, fmt); vfprintf(f, fmt, ap); va_end(ap);
    fputc('\n', f); fclose(f);
}

#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxgi.lib")

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND, UINT, WPARAM, LPARAM);

// ---------------------------------------------------------------------------
// Globals
// ---------------------------------------------------------------------------
static bool                    g_imguiInit = false;
static bool                    g_showMenu  = true;
static HWND                    g_hwnd      = nullptr;
static WNDPROC                 g_oWndProc  = nullptr;

static ID3D12Device*           g_device    = nullptr;
static ID3D12CommandQueue*     g_cmdQueue  = nullptr;
static ID3D12GraphicsCommandList* g_cmdList = nullptr;
static ID3D12DescriptorHeap*   g_rtvHeap   = nullptr;
static UINT                    g_bufCount  = 0;

// GPU sync so we never reset a command allocator that's still in flight
// (omitting this is the classic cause of DXGI_ERROR_DEVICE_REMOVED).
static ID3D12Fence*            g_fence     = nullptr;
static UINT64                  g_fenceVal  = 0;
static HANDLE                  g_fenceEvent = nullptr;

struct FrameCtx {
    ID3D12CommandAllocator*     alloc = nullptr;
    ID3D12Resource*             rt    = nullptr;
    D3D12_CPU_DESCRIPTOR_HANDLE rtv   = {};
};
static std::vector<FrameCtx>   g_frames;

// Simple shader-visible SRV descriptor heap allocator for ImGui (fonts +
// dynamic textures). 1.92's DX12 backend allocates descriptors via callbacks.
struct SrvHeap {
    ID3D12DescriptorHeap*       heap = nullptr;
    UINT                        inc  = 0;
    D3D12_CPU_DESCRIPTOR_HANDLE cpu0 = {};
    D3D12_GPU_DESCRIPTOR_HANDLE gpu0 = {};
    std::vector<UINT>           freeList;

    bool init(ID3D12Device* dev, UINT count) {
        D3D12_DESCRIPTOR_HEAP_DESC d = {};
        d.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        d.NumDescriptors = count;
        d.Flags          = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        if (FAILED(dev->CreateDescriptorHeap(&d, IID_PPV_ARGS(&heap)))) return false;
        inc  = dev->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        cpu0 = heap->GetCPUDescriptorHandleForHeapStart();
        gpu0 = heap->GetGPUDescriptorHandleForHeapStart();
        freeList.reserve(count);
        for (UINT i = count; i > 0; --i) freeList.push_back(i - 1);
        return true;
    }
    void alloc(D3D12_CPU_DESCRIPTOR_HANDLE* c, D3D12_GPU_DESCRIPTOR_HANDLE* g) {
        UINT idx = freeList.back(); freeList.pop_back();
        c->ptr = cpu0.ptr + (SIZE_T)idx * inc;
        g->ptr = gpu0.ptr + (UINT64)idx * inc;
    }
    void free(D3D12_CPU_DESCRIPTOR_HANDLE c, D3D12_GPU_DESCRIPTOR_HANDLE) {
        UINT idx = (UINT)((c.ptr - cpu0.ptr) / inc);
        freeList.push_back(idx);
    }
};
static SrvHeap g_srv;
static void SrvAllocFn(ImGui_ImplDX12_InitInfo*, D3D12_CPU_DESCRIPTOR_HANDLE* c, D3D12_GPU_DESCRIPTOR_HANDLE* g) { g_srv.alloc(c, g); }
static void SrvFreeFn (ImGui_ImplDX12_InitInfo*, D3D12_CPU_DESCRIPTOR_HANDLE c, D3D12_GPU_DESCRIPTOR_HANDLE g)  { g_srv.free(c, g); }

// ---------------------------------------------------------------------------
// Hook typedefs + originals
// ---------------------------------------------------------------------------
typedef HRESULT (WINAPI* Present_t)(IDXGISwapChain3*, UINT, UINT);
typedef HRESULT (WINAPI* ResizeBuffers_t)(IDXGISwapChain3*, UINT, UINT, UINT, DXGI_FORMAT, UINT);
typedef void    (WINAPI* ExecuteCommandLists_t)(ID3D12CommandQueue*, UINT, ID3D12CommandList* const*);
static Present_t             oPresent = nullptr;
static ResizeBuffers_t       oResize  = nullptr;
static ExecuteCommandLists_t oExec    = nullptr;

// ---------------------------------------------------------------------------
static void CleanupRenderTargets() {
    for (auto& f : g_frames) { if (f.rt) { f.rt->Release(); f.rt = nullptr; } }
}
static void CreateRenderTargets(IDXGISwapChain3* sc) {
    D3D12_CPU_DESCRIPTOR_HANDLE h = g_rtvHeap->GetCPUDescriptorHandleForHeapStart();
    UINT inc = g_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    for (UINT i = 0; i < g_bufCount; i++) {
        ID3D12Resource* buf = nullptr;
        sc->GetBuffer(i, IID_PPV_ARGS(&buf));
        g_device->CreateRenderTargetView(buf, nullptr, h);
        g_frames[i].rt  = buf;
        g_frames[i].rtv = h;
        h.ptr += inc;
    }
}

// ---------------------------------------------------------------------------
static LRESULT CALLBACK hkWndProc(HWND h, UINT msg, WPARAM w, LPARAM l) {
    if (g_showMenu) {
        ImGui_ImplWin32_WndProcHandler(h, msg, w, l);
        ImGuiIO& io = ImGui::GetIO();
        if (io.WantCaptureMouse    && msg >= WM_MOUSEFIRST && msg <= WM_MOUSELAST) return 1;
        if (io.WantCaptureKeyboard && msg >= WM_KEYFIRST   && msg <= WM_KEYLAST)   return 1;
    }
    return CallWindowProcW(g_oWndProc, h, msg, w, l);
}

// ---------------------------------------------------------------------------
static bool InitImGui(IDXGISwapChain3* sc) {
    if (FAILED(sc->GetDevice(IID_PPV_ARGS(&g_device)))) { LOG("GetDevice FAILED"); return false; }

    DXGI_SWAP_CHAIN_DESC desc = {};
    sc->GetDesc(&desc);
    g_hwnd     = desc.OutputWindow;
    g_bufCount = desc.BufferCount;
    g_frames.resize(g_bufCount);
    LOG("InitImGui: fmt=%d bufCount=%u hwnd=%p sampleCount=%u flags=0x%x",
        (int)desc.BufferDesc.Format, g_bufCount, (void*)g_hwnd, desc.SampleDesc.Count, desc.Flags);

    // RTV heap (one per back buffer)
    D3D12_DESCRIPTOR_HEAP_DESC rd = {};
    rd.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    rd.NumDescriptors = g_bufCount;
    rd.Flags          = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    if (FAILED(g_device->CreateDescriptorHeap(&rd, IID_PPV_ARGS(&g_rtvHeap)))) return false;

    // SRV heap for ImGui
    if (!g_srv.init(g_device, 64)) return false;

    // Per-frame command allocators + one command list
    for (UINT i = 0; i < g_bufCount; i++)
        if (FAILED(g_device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
                IID_PPV_ARGS(&g_frames[i].alloc)))) return false;
    if (FAILED(g_device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT,
            g_frames[0].alloc, nullptr, IID_PPV_ARGS(&g_cmdList)))) return false;
    g_cmdList->Close();

    if (FAILED(g_device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&g_fence)))) return false;
    g_fenceEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (!g_fenceEvent) return false;

    CreateRenderTargets(sc);

    // ImGui
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    ImGui::StyleColorsDark();

    float scale = 1.0f;
    UINT dpi = GetDpiForWindow(g_hwnd);
    if (dpi) scale = (float)dpi / 96.0f;
    if (scale < 1.0f) scale = 1.0f;
    ImGui::GetStyle().ScaleAllSizes(scale);
    io.FontGlobalScale = scale;

    ImGui_ImplWin32_Init(g_hwnd);

    ImGui_ImplDX12_InitInfo info = {};
    info.Device               = g_device;
    info.CommandQueue         = g_cmdQueue;
    info.NumFramesInFlight    = g_bufCount;
    info.RTVFormat            = desc.BufferDesc.Format;
    info.SrvDescriptorHeap    = g_srv.heap;
    info.SrvDescriptorAllocFn = SrvAllocFn;
    info.SrvDescriptorFreeFn  = SrvFreeFn;
    if (!ImGui_ImplDX12_Init(&info)) return false;

    g_oWndProc = (WNDPROC)SetWindowLongPtrW(g_hwnd, GWLP_WNDPROC, (LONG_PTR)hkWndProc);
    LOG("InitImGui OK (queue=%p)", (void*)g_cmdQueue);
    return true;
}

static void DrawMenu() {
    ImGui::SetNextWindowSize(ImVec2(440, 300), ImGuiCond_FirstUseEver);
    ImGui::Begin("In-Game Overlay (CE-ImGui)");
    ImGui::Text("Dear ImGui %s  |  %.0f FPS", IMGUI_VERSION, ImGui::GetIO().Framerate);
    ImGui::Text("Rendering INSIDE the game via DX12 hook.");
    ImGui::Separator();
    ImGui::TextDisabled("Press INSERT to hide/show this menu.");
    if (ImGui::CollapsingHeader("Proof of concept", ImGuiTreeNodeFlags_DefaultOpen)) {
        static bool a = false; ImGui::Checkbox("A toggle", &a);
        static float f = 0.5f; ImGui::SliderFloat("A slider", &f, 0.0f, 1.0f);
        if (ImGui::Button("A button")) { /* hook trainer actions here */ }
    }
    ImGui::End();
}

// ---------------------------------------------------------------------------
static HRESULT WINAPI hkPresent(IDXGISwapChain3* sc, UINT sync, UINT flags) {
    static bool dead = false;
    if (dead) return oPresent(sc, sync, flags);

    if (!g_imguiInit) {
        if (!g_cmdQueue) return oPresent(sc, sync, flags); // wait for queue capture
        if (!InitImGui(sc)) { LOG("InitImGui returned false"); dead = true; return oPresent(sc, sync, flags); }
        g_imguiInit = true;
    }
    static int frameNo = 0;

    // toggle (edge-triggered)
    static bool prev = false;
    bool down = (GetAsyncKeyState(VK_INSERT) & 0x8000) != 0;
    if (down && !prev) g_showMenu = !g_showMenu;
    prev = down;

    ImGui::GetIO().MouseDrawCursor = g_showMenu;

    ImGui_ImplDX12_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();
    if (g_showMenu) DrawMenu();
    ImGui::Render();

    UINT idx = sc->GetCurrentBackBufferIndex();
    if (frameNo < 5) LOG("frame %d: idx=%u rt=%p", frameNo, idx, (void*)g_frames[idx].rt);
    FrameCtx& fr = g_frames[idx];
    fr.alloc->Reset();

    D3D12_RESOURCE_BARRIER b = {};
    b.Type  = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    b.Transition.pResource   = fr.rt;
    b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    b.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
    b.Transition.StateAfter  = D3D12_RESOURCE_STATE_RENDER_TARGET;

    g_cmdList->Reset(fr.alloc, nullptr);
    g_cmdList->ResourceBarrier(1, &b);
    g_cmdList->OMSetRenderTargets(1, &fr.rtv, FALSE, nullptr);
    g_cmdList->SetDescriptorHeaps(1, &g_srv.heap);
    ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), g_cmdList);
    b.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
    b.Transition.StateAfter  = D3D12_RESOURCE_STATE_PRESENT;
    g_cmdList->ResourceBarrier(1, &b);
    g_cmdList->Close();

    ID3D12CommandList* lists[] = { g_cmdList };
    g_cmdQueue->ExecuteCommandLists(1, lists);

    // Wait for the GPU to finish our overlay work before letting this frame's
    // allocator be reused next time around. Bounded wait so a queue mismatch
    // can never hard-hang the game's render thread.
    const UINT64 v = ++g_fenceVal;
    if (SUCCEEDED(g_cmdQueue->Signal(g_fence, v)) && g_fence->GetCompletedValue() < v) {
        if (SUCCEEDED(g_fence->SetEventOnCompletion(v, g_fenceEvent)))
            WaitForSingleObject(g_fenceEvent, 1000);
    }

    HRESULT rr = g_device->GetDeviceRemovedReason();
    if (rr != S_OK) { LOG("DEVICE REMOVED at frame %d reason=0x%08X", frameNo, (unsigned)rr); dead = true; }
    else if (frameNo < 5) LOG("frame %d rendered OK", frameNo);
    frameNo++;

    return oPresent(sc, sync, flags);
}

static HRESULT WINAPI hkResize(IDXGISwapChain3* sc, UINT n, UINT w, UINT h, DXGI_FORMAT fmt, UINT flags) {
    if (g_imguiInit) CleanupRenderTargets();
    HRESULT hr = oResize(sc, n, w, h, fmt, flags);
    if (g_imguiInit) { DXGI_SWAP_CHAIN_DESC d={}; sc->GetDesc(&d); g_bufCount=d.BufferCount; CreateRenderTargets(sc); }
    return hr;
}

static void WINAPI hkExec(ID3D12CommandQueue* q, UINT n, ID3D12CommandList* const* l) {
    if (!g_cmdQueue && q) {
        D3D12_COMMAND_QUEUE_DESC d = q->GetDesc();
        if (d.Type == D3D12_COMMAND_LIST_TYPE_DIRECT) { g_cmdQueue = q; LOG("captured DIRECT queue %p", (void*)q); }
    }
    oExec(q, n, l);
}

// ---------------------------------------------------------------------------
// Harvest vtable pointers from a throwaway device/swapchain/queue.
// ---------------------------------------------------------------------------
static bool GetVTables(void** outPresent, void** outResize, void** outExec) {
    HMODULE d3d12 = GetModuleHandleW(L"d3d12.dll");
    if (!d3d12) return false;
    auto pD3D12CreateDevice = (decltype(&D3D12CreateDevice))GetProcAddress(d3d12, "D3D12CreateDevice");
    if (!pD3D12CreateDevice) return false;

    ID3D12Device* dev = nullptr;
    if (FAILED(pD3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&dev)))) return false;

    D3D12_COMMAND_QUEUE_DESC qd = {};
    qd.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    ID3D12CommandQueue* queue = nullptr;
    if (FAILED(dev->CreateCommandQueue(&qd, IID_PPV_ARGS(&queue)))) { dev->Release(); return false; }

    // dummy window
    WNDCLASSEXW wc = { sizeof(wc) };
    wc.lpfnWndProc = DefWindowProcW;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"CEImGuiOvDummy";
    RegisterClassExW(&wc);
    HWND hwnd = CreateWindowW(wc.lpszClassName, L"", WS_OVERLAPPEDWINDOW, 0,0,64,64, nullptr,nullptr,wc.hInstance,nullptr);

    IDXGIFactory4* factory = nullptr;
    if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory)))) {
        queue->Release(); dev->Release(); DestroyWindow(hwnd); UnregisterClassW(wc.lpszClassName, wc.hInstance); return false;
    }

    DXGI_SWAP_CHAIN_DESC1 scd = {};
    scd.BufferCount = 2;
    scd.Width = 64; scd.Height = 64;
    scd.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    scd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    scd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    scd.SampleDesc.Count = 1;

    IDXGISwapChain1* swap1 = nullptr;
    HRESULT hr = factory->CreateSwapChainForHwnd(queue, hwnd, &scd, nullptr, nullptr, &swap1);
    bool ok = false;
    if (SUCCEEDED(hr) && swap1) {
        void** scV = *(void***)swap1;     // IDXGISwapChain vtable
        void** cqV = *(void***)queue;     // ID3D12CommandQueue vtable
        *outPresent = scV[8];             // IDXGISwapChain::Present
        *outResize  = scV[13];            // IDXGISwapChain::ResizeBuffers
        *outExec    = cqV[10];            // ID3D12CommandQueue::ExecuteCommandLists
        ok = true;
        swap1->Release();
    }
    factory->Release(); queue->Release(); dev->Release();
    DestroyWindow(hwnd); UnregisterClassW(wc.lpszClassName, wc.hInstance);
    return ok;
}

// ---------------------------------------------------------------------------
static DWORD WINAPI InitThread(LPVOID) {
    void *pPresent = nullptr, *pResize = nullptr, *pExec = nullptr;
    // The game may not have created its device the instant we inject; retry.
    for (int tries = 0; tries < 200; tries++) {
        if (GetModuleHandleW(L"d3d12.dll") && GetVTables(&pPresent, &pResize, &pExec)) break;
        Sleep(50);
    }
    if (!pPresent) { OutputDebugStringA("[Overlay] failed to get vtables\n"); return 0; }

    if (MH_Initialize() != MH_OK) return 0;
    MH_CreateHook(pPresent, &hkPresent, (void**)&oPresent);
    MH_CreateHook(pResize,  &hkResize,  (void**)&oResize);
    MH_CreateHook(pExec,    &hkExec,    (void**)&oExec);
    MH_EnableHook(MH_ALL_HOOKS);
    OutputDebugStringA("[Overlay] hooks installed\n");
    return 0;
}

BOOL APIENTRY DllMain(HMODULE h, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(h);
        CreateThread(nullptr, 0, InitThread, nullptr, 0, nullptr);
    }
    return TRUE;
}
