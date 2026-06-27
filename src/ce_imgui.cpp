// ce_imgui.cpp — plugin state, CE plugin exports, Lua module entry, frame driver.

#include "ce_imgui.h"
#include <cstdio>

namespace CEImGui {

static PluginState g_state;
PluginState& State() { return g_state; }
static char g_pluginName[] = "CE-ImGui (open source)";
static bool g_shutdown = false;

// Called every ~16ms by a CE Lua timer, on CE's main GUI thread.
static int l_frame(lua_State* L) {
    if (!g_shutdown) RendererFrame();
    return 0;
}

// Bootstrap a CE timer (runs on the main thread) to pump frames.
static const char* kBootstrap =
    "if __ceimgui_timer == nil then\n"
    "  local ok, t = pcall(function() return createTimer(getMainForm()) end)\n"
    "  if not ok or t == nil then t = createTimer(nil) end\n"
    "  t.Interval = 16\n"
    "  t.OnTimer  = function() __ceimgui_frame() end\n"
    "  __ceimgui_timer = t\n"
    "end\n";

static void InstallEverything(lua_State* L) {
    g_state.L = L;
    if (!g_state.luaRegistered) {
        RegisterImGui(L);
        lua_pushcfunction(L, l_frame);
        lua_setglobal(L, "__ceimgui_frame");
        g_state.luaRegistered = true;
    }
    g_shutdown = false;
    if (luaL_dostring(L, kBootstrap) != LUA_OK) {
        OutputDebugStringA("[CE-ImGui] timer bootstrap error: ");
        OutputDebugStringA(lua_tostring(L, -1)); OutputDebugStringA("\n");
        lua_pop(L, 1);
    }
}

} // namespace CEImGui

// ============================================================================
// Lua C module entry point — `require("CEImGui")` calls this with CE's state.
// This is the reliable path (proven previously); plugin exports below are a
// secondary convenience.
// ============================================================================
extern "C" __declspec(dllexport) int luaopen_CEImGui(lua_State* L) {
    OutputDebugStringA("[CE-ImGui] luaopen_CEImGui\n");
    CEImGui::InstallEverything(L);
    lua_getglobal(L, "ImGui");   // return the module table
    return 1;
}

// ============================================================================
// CE plugin SDK exports
// ============================================================================
extern "C" {

__declspec(dllexport) BOOL __stdcall CEPlugin_GetVersion(PPluginVersion pv, int sz) {
    pv->version    = CESDK_VERSION;
    pv->pluginname = CEImGui::g_pluginName;
    return TRUE;
}

__declspec(dllexport) BOOL __stdcall CEPlugin_InitializePlugin(PExportedFunctions ef, int pluginid) {
    OutputDebugStringA("[CE-ImGui] CEPlugin_InitializePlugin\n");
    auto& s = CEImGui::State();
    s.pluginId = pluginid;
    int copy = ef->sizeofExportedFunctions < (int)sizeof(ExportedFunctions)
                 ? ef->sizeofExportedFunctions : (int)sizeof(ExportedFunctions);
    memcpy(&s.ce, ef, copy);
    return TRUE;
}

__declspec(dllexport) BOOL __stdcall CEPlugin_DisablePlugin(void) {
    OutputDebugStringA("[CE-ImGui] CEPlugin_DisablePlugin\n");
    CEImGui::g_shutdown = true;
    CEImGui::ClearForms();
    CEImGui::RendererShutdown();
    return TRUE;
}

// Optional: register bindings when CE opens a process (older API path).
__declspec(dllexport) BOOL __stdcall CEPlugin_Lua5OpenProcess(DWORD, lua_State* L) {
    CEImGui::InstallEverything(L);
    return TRUE;
}
__declspec(dllexport) BOOL __stdcall CEPlugin_Lua5CloseProcess(DWORD, lua_State*) { return TRUE; }

} // extern "C"

BOOL APIENTRY DllMain(HMODULE h, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) DisableThreadLibraryCalls(h);
    return TRUE;
}
