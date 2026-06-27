#pragma once
// ce_imgui.h — shared declarations for the CE-ImGui plugin.
//
// Architecture (the deliberate rewrite):
//   * Single-threaded. Everything runs on Cheat Engine's MAIN GUI thread,
//     driven by a CE Lua timer that ticks ~60fps and calls CEImGui_Frame().
//   * Immediate mode. We expose raw Dear ImGui functions to Lua. Each "form"
//     just owns an OnRender Lua callback that runs between NewFrame and Render.
//   * Because we're on the main thread, OnRender callbacks can call CE memory
//     APIs (readInteger/writeFloat/autoAssemble/...) directly — no marshaling.

#include "cepluginsdk.h"
#include <cstdint>

namespace CEImGui {

// ---- Plugin-wide state ----
struct PluginState {
    lua_State*        L            = nullptr;   // CE's main Lua state
    ExportedFunctions ce           = {};        // CE-provided function table
    int               pluginId     = -1;
    bool              luaRegistered = false;     // ImGui table registered?
    bool              rendererReady = false;     // D3D11 + ImGui initialised?
    bool              wantRenderer  = false;     // a form asked for a window
    float             dpiScale     = 1.0f;       // detected UI scale (4K aware)
};

PluginState& State();

// ---- Renderer (renderer.cpp) ----
// All called on the main thread.
bool RendererInit();        // lazy: create window + D3D11 + ImGui context
void RendererShutdown();
void RendererFrame();       // one full NewFrame -> OnRender callbacks -> present
void RendererSetScale(float scale);

// ---- Lua bindings (imgui_bindings.cpp) ----
void RegisterImGui(lua_State* L);     // builds the global `ImGui` table + forms
void RenderForms();                   // iterate forms, run their OnRender (called inside a frame)
void ClearForms();

} // namespace CEImGui
