// imgui_bindings.cpp — immediate-mode Dear ImGui exposed to Cheat Engine Lua.
//
// Design: every binding is a thin wrapper over the real ImGui function, so the
// full feature set comes "for free" and new functions are one-liners. A "form"
// is just a Lua table with an OnRender callback that runs between NewFrame and
// Render (see RenderForms). Because everything is on CE's main thread, OnRender
// callbacks may freely call CE memory APIs.

#include "ce_imgui.h"
#include "imgui.h"

#include <string>
#include <vector>
#include <unordered_map>
#include <cfloat>

namespace CEImGui {

static const char* FORMS_KEY    = "CEImGui.forms";
static const char* FORM_MT      = "CEImGui.FormMeta";

// ---- small argument helpers -------------------------------------------------
static float       optf  (lua_State* L, int i, float d)      { return lua_isnoneornil(L,i)?d:(float)luaL_checknumber(L,i); }
static int         opti  (lua_State* L, int i, int d)        { return lua_isnoneornil(L,i)?d:(int)luaL_checkinteger(L,i); }
static bool        optb  (lua_State* L, int i, bool d)       { return lua_isnoneornil(L,i)?d:(lua_toboolean(L,i)!=0); }
static const char* opts  (lua_State* L, int i, const char* d){ return lua_isnoneornil(L,i)?d:luaL_checkstring(L,i); }
static const char* cstr  (lua_State* L, int i)               { return luaL_checkstring(L,i); }

static void readFloats(lua_State* L, int idx, float* out, int n) {
    for (int k = 0; k < n; k++) {
        lua_rawgeti(L, idx, k + 1);
        out[k] = (float)lua_tonumber(L, -1);
        lua_pop(L, 1);
    }
}
static void pushFloats(lua_State* L, const float* in, int n) {
    lua_createtable(L, n, 0);
    for (int k = 0; k < n; k++) { lua_pushnumber(L, in[k]); lua_rawseti(L, -2, k + 1); }
}

// ============================================================================
// Windows
// ============================================================================
static int l_Begin(lua_State* L) {
    const char* name = cstr(L, 1);
    ImGuiWindowFlags flags = (ImGuiWindowFlags)opti(L, 3, 0);
    bool open = true, hasClose = !lua_isnoneornil(L, 2);
    if (hasClose) open = lua_toboolean(L, 2) != 0;
    bool visible = ImGui::Begin(name, hasClose ? &open : nullptr, flags);
    lua_pushboolean(L, visible);
    lua_pushboolean(L, open);
    return 2; // visible, open
}
static int l_End(lua_State* L) { ImGui::End(); return 0; }

static int l_BeginChild(lua_State* L) {
    const char* id = cstr(L, 1);
    ImVec2 size(optf(L,2,0), optf(L,3,0));
    bool border = optb(L, 4, false);
    ImGuiChildFlags cf = border ? ImGuiChildFlags_Borders : 0;
    ImGuiWindowFlags wf = (ImGuiWindowFlags)opti(L, 5, 0);
    lua_pushboolean(L, ImGui::BeginChild(id, size, cf, wf));
    return 1;
}
static int l_EndChild(lua_State* L) { ImGui::EndChild(); return 0; }

static int l_SetNextWindowSize(lua_State* L) { ImGui::SetNextWindowSize(ImVec2(optf(L,1,0),optf(L,2,0)), opti(L,3,ImGuiCond_Always)); return 0; }
static int l_SetNextWindowPos (lua_State* L) { ImGui::SetNextWindowPos (ImVec2(optf(L,1,0),optf(L,2,0)), opti(L,3,ImGuiCond_Always)); return 0; }
static int l_SetNextWindowCollapsed(lua_State* L){ ImGui::SetNextWindowCollapsed(optb(L,1,false), opti(L,2,ImGuiCond_Always)); return 0; }
static int l_GetWindowWidth (lua_State* L){ lua_pushnumber(L, ImGui::GetWindowWidth());  return 1; }
static int l_GetWindowHeight(lua_State* L){ lua_pushnumber(L, ImGui::GetWindowHeight()); return 1; }

// ============================================================================
// Text
// ============================================================================
static int l_Text         (lua_State* L){ ImGui::TextUnformatted(cstr(L,1)); return 0; }
static int l_TextDisabled (lua_State* L){ ImGui::TextDisabled("%s", cstr(L,1)); return 0; }
static int l_TextWrapped  (lua_State* L){ ImGui::TextWrapped("%s", cstr(L,1)); return 0; }
static int l_LabelText    (lua_State* L){ ImGui::LabelText(cstr(L,1), "%s", cstr(L,2)); return 0; }
static int l_BulletText   (lua_State* L){ ImGui::BulletText("%s", cstr(L,1)); return 0; }
static int l_SeparatorText(lua_State* L){ ImGui::SeparatorText(cstr(L,1)); return 0; }
static int l_TextColored  (lua_State* L){
    // TextColored(text, r,g,b[,a])
    const char* t = cstr(L,1);
    ImVec4 c(optf(L,2,1),optf(L,3,1),optf(L,4,1),optf(L,5,1));
    ImGui::TextColored(c, "%s", t); return 0;
}

// ============================================================================
// Buttons / basic
// ============================================================================
static int l_Button(lua_State* L){
    lua_pushboolean(L, ImGui::Button(cstr(L,1), ImVec2(optf(L,2,0), optf(L,3,0)))); return 1;
}
static int l_SmallButton (lua_State* L){ lua_pushboolean(L, ImGui::SmallButton(cstr(L,1))); return 1; }
static int l_InvisibleButton(lua_State* L){ lua_pushboolean(L, ImGui::InvisibleButton(cstr(L,1), ImVec2(optf(L,2,1),optf(L,3,1)))); return 1; }
static int l_ArrowButton(lua_State* L){ lua_pushboolean(L, ImGui::ArrowButton(cstr(L,1), (ImGuiDir)opti(L,2,ImGuiDir_Right))); return 1; }
static int l_Bullet(lua_State* L){ ImGui::Bullet(); return 0; }

static int l_Checkbox(lua_State* L){
    const char* label = cstr(L,1);
    bool v = lua_toboolean(L,2)!=0;
    bool changed = ImGui::Checkbox(label, &v);
    lua_pushboolean(L, changed); lua_pushboolean(L, v); return 2;
}
static int l_RadioButton(lua_State* L){
    lua_pushboolean(L, ImGui::RadioButton(cstr(L,1), optb(L,2,false))); return 1;
}
static int l_ProgressBar(lua_State* L){
    float frac = (float)luaL_checknumber(L,1);
    ImVec2 size(optf(L,2,-1), optf(L,3,0));
    const char* overlay = lua_isnoneornil(L,4)?nullptr:luaL_checkstring(L,4);
    ImGui::ProgressBar(frac, size, overlay); return 0;
}

// ============================================================================
// Sliders / Drags
// ============================================================================
static int l_SliderFloat(lua_State* L){
    const char* lbl=cstr(L,1); float v=(float)luaL_checknumber(L,2);
    float mn=(float)luaL_checknumber(L,3), mx=(float)luaL_checknumber(L,4);
    const char* fmt=opts(L,5,"%.3f");
    bool ch=ImGui::SliderFloat(lbl,&v,mn,mx,fmt);
    lua_pushboolean(L,ch); lua_pushnumber(L,v); return 2;
}
static int l_SliderInt(lua_State* L){
    const char* lbl=cstr(L,1); int v=(int)luaL_checkinteger(L,2);
    int mn=(int)luaL_checkinteger(L,3), mx=(int)luaL_checkinteger(L,4);
    bool ch=ImGui::SliderInt(lbl,&v,mn,mx);
    lua_pushboolean(L,ch); lua_pushinteger(L,v); return 2;
}
static int l_DragFloat(lua_State* L){
    const char* lbl=cstr(L,1); float v=(float)luaL_checknumber(L,2);
    float spd=optf(L,3,1.0f), mn=optf(L,4,0.0f), mx=optf(L,5,0.0f);
    const char* fmt=opts(L,6,"%.3f");
    bool ch=ImGui::DragFloat(lbl,&v,spd,mn,mx,fmt);
    lua_pushboolean(L,ch); lua_pushnumber(L,v); return 2;
}
static int l_DragInt(lua_State* L){
    const char* lbl=cstr(L,1); int v=(int)luaL_checkinteger(L,2);
    float spd=optf(L,3,1.0f); int mn=opti(L,4,0), mx=opti(L,5,0);
    bool ch=ImGui::DragInt(lbl,&v,spd,mn,mx);
    lua_pushboolean(L,ch); lua_pushinteger(L,v); return 2;
}

// ============================================================================
// Input
// ============================================================================
static int l_InputText(lua_State* L){
    const char* lbl=cstr(L,1);
    size_t len=0; const char* cur=luaL_optlstring(L,2,"",&len);
    size_t cap=(size_t)opti(L,3,256); if (cap<len+1) cap=len+1;
    ImGuiInputTextFlags flags=(ImGuiInputTextFlags)opti(L,4,0);
    std::vector<char> buf(cap+1,0); memcpy(buf.data(),cur,len);
    bool ch=ImGui::InputText(lbl,buf.data(),buf.size(),flags);
    lua_pushboolean(L,ch); lua_pushstring(L,buf.data()); return 2;
}
static int l_InputTextMultiline(lua_State* L){
    const char* lbl=cstr(L,1);
    size_t len=0; const char* cur=luaL_optlstring(L,2,"",&len);
    size_t cap=(size_t)opti(L,5,4096); if (cap<len+1) cap=len+1;
    ImVec2 size(optf(L,3,0), optf(L,4,0));
    std::vector<char> buf(cap+1,0); memcpy(buf.data(),cur,len);
    bool ch=ImGui::InputTextMultiline(lbl,buf.data(),buf.size(),size,0);
    lua_pushboolean(L,ch); lua_pushstring(L,buf.data()); return 2;
}
static int l_InputInt(lua_State* L){
    const char* lbl=cstr(L,1); int v=(int)luaL_checkinteger(L,2);
    int step=opti(L,3,1), stepfast=opti(L,4,100);
    bool ch=ImGui::InputInt(lbl,&v,step,stepfast);
    lua_pushboolean(L,ch); lua_pushinteger(L,v); return 2;
}
static int l_InputFloat(lua_State* L){
    const char* lbl=cstr(L,1); float v=(float)luaL_checknumber(L,2);
    float step=optf(L,3,0), stepfast=optf(L,4,0);
    const char* fmt=opts(L,5,"%.3f");
    bool ch=ImGui::InputFloat(lbl,&v,step,stepfast,fmt);
    lua_pushboolean(L,ch); lua_pushnumber(L,v); return 2;
}

// ============================================================================
// Color
// ============================================================================
static int l_ColorEdit3(lua_State* L){
    const char* lbl=cstr(L,1); float c[3]={1,1,1};
    if (lua_istable(L,2)) readFloats(L,2,c,3);
    bool ch=ImGui::ColorEdit3(lbl,c,(ImGuiColorEditFlags)opti(L,3,0));
    lua_pushboolean(L,ch); pushFloats(L,c,3); return 2;
}
static int l_ColorEdit4(lua_State* L){
    const char* lbl=cstr(L,1); float c[4]={1,1,1,1};
    if (lua_istable(L,2)) readFloats(L,2,c,4);
    bool ch=ImGui::ColorEdit4(lbl,c,(ImGuiColorEditFlags)opti(L,3,0));
    lua_pushboolean(L,ch); pushFloats(L,c,4); return 2;
}

// ============================================================================
// Combo / Selectable / ListBox
// ============================================================================
static int l_BeginCombo(lua_State* L){ lua_pushboolean(L, ImGui::BeginCombo(cstr(L,1), cstr(L,2), (ImGuiComboFlags)opti(L,3,0))); return 1; }
static int l_EndCombo  (lua_State* L){ ImGui::EndCombo(); return 0; }
static int l_Selectable(lua_State* L){
    const char* lbl=cstr(L,1); bool sel=optb(L,2,false);
    bool ch=ImGui::Selectable(lbl,&sel,(ImGuiSelectableFlags)opti(L,3,0));
    lua_pushboolean(L,ch); lua_pushboolean(L,sel); return 2;
}
static int l_BeginListBox(lua_State* L){ lua_pushboolean(L, ImGui::BeginListBox(cstr(L,1), ImVec2(optf(L,2,0),optf(L,3,0)))); return 1; }
static int l_EndListBox(lua_State* L){ ImGui::EndListBox(); return 0; }
// Combo(label, current(1-based), {items}) -> changed, current(1-based)
static int l_Combo(lua_State* L){
    const char* lbl=cstr(L,1);
    int cur=(int)luaL_checkinteger(L,2)-1; // to 0-based
    luaL_checktype(L,3,LUA_TTABLE);
    int n=(int)lua_rawlen(L,3);
    std::vector<const char*> items; std::vector<std::string> store;
    store.reserve(n);
    for (int k=1;k<=n;k++){ lua_rawgeti(L,3,k); store.push_back(lua_tostring(L,-1)?lua_tostring(L,-1):""); lua_pop(L,1); }
    for (auto& s: store) items.push_back(s.c_str());
    bool ch=ImGui::Combo(lbl,&cur,items.data(),n);
    lua_pushboolean(L,ch); lua_pushinteger(L,cur+1); return 2;
}

// ============================================================================
// Trees / Tabs / Headers
// ============================================================================
static int l_TreeNode(lua_State* L){ lua_pushboolean(L, ImGui::TreeNode(cstr(L,1))); return 1; }
static int l_TreePop (lua_State* L){ ImGui::TreePop(); return 0; }
static int l_CollapsingHeader(lua_State* L){ lua_pushboolean(L, ImGui::CollapsingHeader(cstr(L,1),(ImGuiTreeNodeFlags)opti(L,2,0))); return 1; }
static int l_SetNextItemOpen(lua_State* L){ ImGui::SetNextItemOpen(optb(L,1,true), opti(L,2,ImGuiCond_Always)); return 0; }
static int l_BeginTabBar (lua_State* L){ lua_pushboolean(L, ImGui::BeginTabBar(cstr(L,1),(ImGuiTabBarFlags)opti(L,2,0))); return 1; }
static int l_EndTabBar   (lua_State* L){ ImGui::EndTabBar(); return 0; }
static int l_BeginTabItem(lua_State* L){ lua_pushboolean(L, ImGui::BeginTabItem(cstr(L,1))); return 1; }
static int l_EndTabItem  (lua_State* L){ ImGui::EndTabItem(); return 0; }

// ============================================================================
// Tables
// ============================================================================
static int l_BeginTable(lua_State* L){
    const char* id=cstr(L,1); int cols=(int)luaL_checkinteger(L,2);
    ImGuiTableFlags flags=(ImGuiTableFlags)opti(L,3,0);
    lua_pushboolean(L, ImGui::BeginTable(id,cols,flags,ImVec2(optf(L,4,0),optf(L,5,0)))); return 1;
}
static int l_EndTable(lua_State* L){ ImGui::EndTable(); return 0; }
static int l_TableNextRow(lua_State* L){ ImGui::TableNextRow((ImGuiTableRowFlags)opti(L,1,0), optf(L,2,0)); return 0; }
static int l_TableNextColumn(lua_State* L){ lua_pushboolean(L, ImGui::TableNextColumn()); return 1; }
static int l_TableSetColumnIndex(lua_State* L){ lua_pushboolean(L, ImGui::TableSetColumnIndex((int)luaL_checkinteger(L,1))); return 1; }
static int l_TableSetupColumn(lua_State* L){ ImGui::TableSetupColumn(cstr(L,1),(ImGuiTableColumnFlags)opti(L,2,0), optf(L,3,0)); return 0; }
static int l_TableSetupScrollFreeze(lua_State* L){ ImGui::TableSetupScrollFreeze(opti(L,1,0), opti(L,2,0)); return 0; }
static int l_TableHeadersRow(lua_State* L){ ImGui::TableHeadersRow(); return 0; }

// ============================================================================
// Menus / Popups / Tooltips
// ============================================================================
static int l_BeginMenuBar(lua_State* L){ lua_pushboolean(L, ImGui::BeginMenuBar()); return 1; }
static int l_EndMenuBar  (lua_State* L){ ImGui::EndMenuBar(); return 0; }
static int l_BeginMainMenuBar(lua_State* L){ lua_pushboolean(L, ImGui::BeginMainMenuBar()); return 1; }
static int l_EndMainMenuBar(lua_State* L){ ImGui::EndMainMenuBar(); return 0; }
static int l_BeginMenu(lua_State* L){ lua_pushboolean(L, ImGui::BeginMenu(cstr(L,1), optb(L,2,true))); return 1; }
static int l_EndMenu  (lua_State* L){ ImGui::EndMenu(); return 0; }
static int l_MenuItem (lua_State* L){
    const char* lbl=cstr(L,1); const char* sc=lua_isnoneornil(L,2)?nullptr:luaL_checkstring(L,2);
    bool sel=optb(L,3,false);
    bool clicked=ImGui::MenuItem(lbl,sc,&sel,optb(L,4,true));
    lua_pushboolean(L,clicked); lua_pushboolean(L,sel); return 2;
}
static int l_OpenPopup(lua_State* L){ ImGui::OpenPopup(cstr(L,1)); return 0; }
static int l_BeginPopup(lua_State* L){ lua_pushboolean(L, ImGui::BeginPopup(cstr(L,1),(ImGuiWindowFlags)opti(L,2,0))); return 1; }
static int l_BeginPopupModal(lua_State* L){
    bool open=true; bool vis=ImGui::BeginPopupModal(cstr(L,1), &open,(ImGuiWindowFlags)opti(L,2,0));
    lua_pushboolean(L,vis); lua_pushboolean(L,open); return 2;
}
static int l_BeginPopupContextItem(lua_State* L){ lua_pushboolean(L, ImGui::BeginPopupContextItem(lua_isnoneornil(L,1)?nullptr:luaL_checkstring(L,1))); return 1; }
static int l_EndPopup(lua_State* L){ ImGui::EndPopup(); return 0; }
static int l_CloseCurrentPopup(lua_State* L){ ImGui::CloseCurrentPopup(); return 0; }
static int l_BeginTooltip(lua_State* L){ lua_pushboolean(L, ImGui::BeginTooltip()); return 1; }
static int l_EndTooltip(lua_State* L){ ImGui::EndTooltip(); return 0; }
static int l_SetTooltip(lua_State* L){ ImGui::SetTooltip("%s", cstr(L,1)); return 0; }

// ============================================================================
// Layout
// ============================================================================
static int l_Separator (lua_State* L){ ImGui::Separator(); return 0; }
static int l_SameLine  (lua_State* L){ ImGui::SameLine(optf(L,1,0), optf(L,2,-1)); return 0; }
static int l_NewLine   (lua_State* L){ ImGui::NewLine(); return 0; }
static int l_Spacing   (lua_State* L){ ImGui::Spacing(); return 0; }
static int l_Dummy     (lua_State* L){ ImGui::Dummy(ImVec2(optf(L,1,0),optf(L,2,0))); return 0; }
static int l_Indent    (lua_State* L){ ImGui::Indent(optf(L,1,0)); return 0; }
static int l_Unindent  (lua_State* L){ ImGui::Unindent(optf(L,1,0)); return 0; }
static int l_BeginGroup(lua_State* L){ ImGui::BeginGroup(); return 0; }
static int l_EndGroup  (lua_State* L){ ImGui::EndGroup(); return 0; }
static int l_SetNextItemWidth(lua_State* L){ ImGui::SetNextItemWidth(optf(L,1,0)); return 0; }
static int l_PushItemWidth(lua_State* L){ ImGui::PushItemWidth(optf(L,1,0)); return 0; }
static int l_PopItemWidth (lua_State* L){ ImGui::PopItemWidth(); return 0; }
static int l_GetContentRegionAvail(lua_State* L){ ImVec2 v=ImGui::GetContentRegionAvail(); lua_pushnumber(L,v.x); lua_pushnumber(L,v.y); return 2; }
static int l_GetCursorPosX(lua_State* L){ lua_pushnumber(L, ImGui::GetCursorPosX()); return 1; }
static int l_GetCursorPosY(lua_State* L){ lua_pushnumber(L, ImGui::GetCursorPosY()); return 1; }
static int l_SetCursorPosX(lua_State* L){ ImGui::SetCursorPosX(optf(L,1,0)); return 0; }
static int l_SetCursorPosY(lua_State* L){ ImGui::SetCursorPosY(optf(L,1,0)); return 0; }

// ============================================================================
// ID stack / item queries / style
// ============================================================================
static int l_PushID(lua_State* L){ if (lua_isnumber(L,1)) ImGui::PushID((int)lua_tointeger(L,1)); else ImGui::PushID(cstr(L,1)); return 0; }
static int l_PopID (lua_State* L){ ImGui::PopID(); return 0; }
static int l_IsItemHovered(lua_State* L){ lua_pushboolean(L, ImGui::IsItemHovered()); return 1; }
static int l_IsItemClicked(lua_State* L){ lua_pushboolean(L, ImGui::IsItemClicked(opti(L,1,0))); return 1; }
static int l_IsItemActive (lua_State* L){ lua_pushboolean(L, ImGui::IsItemActive()); return 1; }
static int l_IsItemFocused(lua_State* L){ lua_pushboolean(L, ImGui::IsItemFocused()); return 1; }
static int l_SetItemDefaultFocus(lua_State* L){ ImGui::SetItemDefaultFocus(); return 0; }
static int l_BeginDisabled(lua_State* L){ ImGui::BeginDisabled(optb(L,1,true)); return 0; }
static int l_EndDisabled  (lua_State* L){ ImGui::EndDisabled(); return 0; }
static int l_PushStyleColor(lua_State* L){
    ImGuiCol idx=(ImGuiCol)luaL_checkinteger(L,1);
    ImGui::PushStyleColor(idx, ImVec4(optf(L,2,1),optf(L,3,1),optf(L,4,1),optf(L,5,1))); return 0;
}
static int l_PopStyleColor(lua_State* L){ ImGui::PopStyleColor(opti(L,1,1)); return 0; }
static int l_StyleColorsDark   (lua_State* L){ ImGui::StyleColorsDark();    ImGui::GetStyle().ScaleAllSizes(State().dpiScale); return 0; }
static int l_StyleColorsLight  (lua_State* L){ ImGui::StyleColorsLight();   ImGui::GetStyle().ScaleAllSizes(State().dpiScale); return 0; }
static int l_StyleColorsClassic(lua_State* L){ ImGui::StyleColorsClassic(); ImGui::GetStyle().ScaleAllSizes(State().dpiScale); return 0; }

// ============================================================================
// Plots / misc utility
// ============================================================================
static int l_PlotLines(lua_State* L){
    const char* lbl=cstr(L,1); luaL_checktype(L,2,LUA_TTABLE);
    int n=(int)lua_rawlen(L,2); std::vector<float> v(n);
    readFloats(L,2,v.data(),n);
    ImGui::PlotLines(lbl, v.data(), n, 0, nullptr, optf(L,3,FLT_MAX), optf(L,4,FLT_MAX), ImVec2(optf(L,5,0),optf(L,6,0)));
    return 0;
}
static int l_GetFrameRate(lua_State* L){ lua_pushnumber(L, ImGui::GetIO().Framerate); return 1; }
static int l_ShowDemoWindow(lua_State* L){ ImGui::ShowDemoWindow(); return 0; }
static int l_ShowMetricsWindow(lua_State* L){ ImGui::ShowMetricsWindow(); return 0; }
static int l_SetScale(lua_State* L){ RendererSetScale((float)luaL_checknumber(L,1)); return 0; }
// Must be called BEFORE the first CreateForm. true = transparent topmost
// click-through overlay (for borderless-fullscreen games); false = normal window.
static int l_SetOverlayMode(lua_State* L){
    if (State().rendererReady) { lua_pushboolean(L,false); return 1; } // too late
    State().overlayMode = (lua_toboolean(L,1)!=0);
    lua_pushboolean(L,true); return 1;
}
static int l_GetScale(lua_State* L){ lua_pushnumber(L, State().dpiScale); return 1; }
static int l_GetVersion(lua_State* L){ lua_pushstring(L, IMGUI_VERSION); return 1; }

// ---- vector sliders / drags (value passed & returned as a Lua array) ----
static int sliderN(lua_State* L, int n){
    const char* lbl=cstr(L,1); float v[4]={0,0,0,0};
    if (lua_istable(L,2)) readFloats(L,2,v,n);
    float mn=(float)luaL_checknumber(L,3), mx=(float)luaL_checknumber(L,4);
    const char* fmt=opts(L,5,"%.3f"); bool ch=false;
    if (n==2) ch=ImGui::SliderFloat2(lbl,v,mn,mx,fmt);
    else if (n==3) ch=ImGui::SliderFloat3(lbl,v,mn,mx,fmt);
    else ch=ImGui::SliderFloat4(lbl,v,mn,mx,fmt);
    lua_pushboolean(L,ch); pushFloats(L,v,n); return 2;
}
static int l_SliderFloat2(lua_State* L){ return sliderN(L,2); }
static int l_SliderFloat3(lua_State* L){ return sliderN(L,3); }
static int l_SliderFloat4(lua_State* L){ return sliderN(L,4); }
static int dragN(lua_State* L, int n){
    const char* lbl=cstr(L,1); float v[4]={0,0,0,0};
    if (lua_istable(L,2)) readFloats(L,2,v,n);
    float spd=optf(L,3,1.0f), mn=optf(L,4,0.0f), mx=optf(L,5,0.0f);
    const char* fmt=opts(L,6,"%.3f"); bool ch=false;
    if (n==2) ch=ImGui::DragFloat2(lbl,v,spd,mn,mx,fmt);
    else if (n==3) ch=ImGui::DragFloat3(lbl,v,spd,mn,mx,fmt);
    else ch=ImGui::DragFloat4(lbl,v,spd,mn,mx,fmt);
    lua_pushboolean(L,ch); pushFloats(L,v,n); return 2;
}
static int l_DragFloat2(lua_State* L){ return dragN(L,2); }
static int l_DragFloat3(lua_State* L){ return dragN(L,3); }

static int l_ColorPicker4(lua_State* L){
    const char* lbl=cstr(L,1); float c[4]={1,1,1,1};
    if (lua_istable(L,2)) readFloats(L,2,c,4);
    bool ch=ImGui::ColorPicker4(lbl,c,(ImGuiColorEditFlags)opti(L,3,0));
    lua_pushboolean(L,ch); pushFloats(L,c,4); return 2;
}
static int l_ColorButton(lua_State* L){
    const char* id=cstr(L,1); float c[4]={1,1,1,1};
    if (lua_istable(L,2)) readFloats(L,2,c,4);
    lua_pushboolean(L, ImGui::ColorButton(id, ImVec4(c[0],c[1],c[2],c[3]),
        (ImGuiColorEditFlags)opti(L,3,0), ImVec2(optf(L,4,0),optf(L,5,0)))); return 1;
}

// ---- window/mouse/clipboard queries ----
static int l_IsWindowHovered(lua_State* L){ lua_pushboolean(L, ImGui::IsWindowHovered()); return 1; }
static int l_IsWindowFocused(lua_State* L){ lua_pushboolean(L, ImGui::IsWindowFocused()); return 1; }
static int l_SetWindowFontScale(lua_State* L){ ImGui::SetWindowFontScale(optf(L,1,1.0f)); return 0; }
static int l_GetMousePos(lua_State* L){ ImVec2 p=ImGui::GetMousePos(); lua_pushnumber(L,p.x); lua_pushnumber(L,p.y); return 2; }
static int l_IsMouseClicked(lua_State* L){ lua_pushboolean(L, ImGui::IsMouseClicked(opti(L,1,0))); return 1; }
static int l_IsMouseDown(lua_State* L){ lua_pushboolean(L, ImGui::IsMouseDown(opti(L,1,0))); return 1; }
static int l_SetClipboardText(lua_State* L){ ImGui::SetClipboardText(cstr(L,1)); return 0; }
static int l_GetClipboardText(lua_State* L){ const char* s=ImGui::GetClipboardText(); lua_pushstring(L, s?s:""); return 1; }

// ============================================================================
// Forms
// ============================================================================
static void getFormsTable(lua_State* L){ lua_getfield(L, LUA_REGISTRYINDEX, FORMS_KEY); }

static int l_form_Show   (lua_State* L){ lua_pushboolean(L,true);  lua_setfield(L,1,"Visible"); return 0; }
static int l_form_Hide   (lua_State* L){ lua_pushboolean(L,false); lua_setfield(L,1,"Visible"); return 0; }
static int l_form_Toggle (lua_State* L){
    lua_getfield(L,1,"Visible"); bool v=lua_toboolean(L,-1)!=0; lua_pop(L,1);
    lua_pushboolean(L,!v); lua_setfield(L,1,"Visible"); return 0;
}
static int l_form_Destroy(lua_State* L){
    // remove this form table from the forms array (by identity)
    getFormsTable(L); int t=lua_gettop(L);
    int n=(int)lua_rawlen(L,t);
    for (int k=1;k<=n;k++){
        lua_rawgeti(L,t,k);
        bool same=lua_rawequal(L,1,-1); lua_pop(L,1);
        if (same){ // shift down
            for (int j=k;j<n;j++){ lua_rawgeti(L,t,j+1); lua_rawseti(L,t,j); }
            lua_pushnil(L); lua_rawseti(L,t,n);
            break;
        }
    }
    lua_pop(L,1); return 0;
}

// ImGui.CreateForm(title[, width, height]) -> form table
static int l_CreateForm(lua_State* L){
    const char* title = opts(L,1,"CE-ImGui Form");
    float w = optf(L,2,420), h = optf(L,3,320);

    lua_newtable(L);                              // form
    lua_pushstring(L,title); lua_setfield(L,-2,"Title");
    lua_pushnumber(L,w);     lua_setfield(L,-2,"Width");
    lua_pushnumber(L,h);     lua_setfield(L,-2,"Height");
    lua_pushboolean(L,true); lua_setfield(L,-2,"Visible");

    luaL_getmetatable(L, FORM_MT);
    lua_setmetatable(L,-2);

    // append to forms registry array
    getFormsTable(L);                             // form, formsTbl
    int n=(int)lua_rawlen(L,-1);
    lua_pushvalue(L,-2);                          // form, formsTbl, form
    lua_rawseti(L,-2,n+1);
    lua_pop(L,1);                                 // form

    // Request the window; actual creation happens on the main thread in the
    // next frame tick (see RendererFrame).
    State().wantRenderer = true;
    return 1;
}

// ============================================================================
// RenderForms — called by the renderer each frame, inside NewFrame..Render
// ============================================================================
static std::unordered_map<int,bool> g_keyPrev;
static int g_visibleForms = 0;
int VisibleFormCount() { return g_visibleForms; }

void RenderForms() {
    lua_State* L = State().L;
    if (!L) return;
    getFormsTable(L);
    int t = lua_gettop(L);
    int n = (int)lua_rawlen(L, t);
    int visCount = 0;

    for (int i = 1; i <= n; i++) {
        lua_rawgeti(L, t, i);          // form
        int f = lua_gettop(L);
        if (!lua_istable(L, f)) { lua_pop(L,1); continue; }

        // Hotkey toggle (edge-triggered)
        lua_getfield(L, f, "ToggleKey");
        if (lua_isnumber(L,-1)) {
            int vk=(int)lua_tointeger(L,-1);
            bool down=(GetAsyncKeyState(vk)&0x8000)!=0;
            if (down && !g_keyPrev[vk]) {
                lua_getfield(L,f,"Visible"); bool vis=lua_toboolean(L,-1)!=0; lua_pop(L,1);
                lua_pushboolean(L,!vis); lua_setfield(L,f,"Visible");
            }
            g_keyPrev[vk]=down;
        }
        lua_pop(L,1);

        lua_getfield(L, f, "Visible");
        bool visible = lua_isnil(L,-1) ? true : (lua_toboolean(L,-1)!=0);
        lua_pop(L,1);
        if (!visible) { lua_pop(L,1); continue; }
        visCount++;

        lua_getfield(L, f, "OnRender");
        bool hasRender = lua_isfunction(L,-1);
        lua_pop(L,1);

        lua_getfield(L, f, "NoAutoBegin");
        bool noAuto = lua_toboolean(L,-1)!=0;
        lua_pop(L,1);

        if (noAuto) {
            if (hasRender) {
                lua_getfield(L, f, "OnRender");
                if (lua_pcall(L, 0, 0, 0) != LUA_OK) {
                    OutputDebugStringA(lua_tostring(L,-1)); OutputDebugStringA("\n"); lua_pop(L,1);
                }
            }
        } else {
            lua_getfield(L,f,"Title"); const char* title=lua_tostring(L,-1); if(!title)title="Form";
            std::string winTitle = std::string(title) + "###ceform" + std::to_string(i);
            lua_pop(L,1);

            lua_getfield(L,f,"Width");  float fw=(float)lua_tonumber(L,-1); lua_pop(L,1);
            lua_getfield(L,f,"Height"); float fh=(float)lua_tonumber(L,-1); lua_pop(L,1);
            ImGui::SetNextWindowSize(ImVec2(fw>0?fw:420, fh>0?fh:320), ImGuiCond_FirstUseEver);

            bool open=true;
            ImGui::Begin(winTitle.c_str(), &open, 0);
            if (hasRender) {
                lua_getfield(L, f, "OnRender");
                if (lua_pcall(L, 0, 0, 0) != LUA_OK) {
                    const char* err=lua_tostring(L,-1);
                    ImGui::TextColored(ImVec4(1,0.4f,0.4f,1), "OnRender error:");
                    ImGui::TextWrapped("%s", err?err:"unknown");
                    OutputDebugStringA(err?err:"unknown"); OutputDebugStringA("\n");
                    lua_pop(L,1);
                }
            }
            ImGui::End();
            if (!open) { lua_pushboolean(L,false); lua_setfield(L,f,"Visible"); }
        }

        lua_pop(L,1); // form
    }
    g_visibleForms = visCount;
    lua_pop(L,1); // forms table
}

void ClearForms() {
    lua_State* L = State().L; if (!L) return;
    lua_newtable(L); lua_setfield(L, LUA_REGISTRYINDEX, FORMS_KEY);
}

// ============================================================================
// Registration
// ============================================================================
static const luaL_Reg kImGuiFuncs[] = {
    // windows
    {"Begin",l_Begin},{"End",l_End},{"BeginChild",l_BeginChild},{"EndChild",l_EndChild},
    {"SetNextWindowSize",l_SetNextWindowSize},{"SetNextWindowPos",l_SetNextWindowPos},
    {"SetNextWindowCollapsed",l_SetNextWindowCollapsed},
    {"GetWindowWidth",l_GetWindowWidth},{"GetWindowHeight",l_GetWindowHeight},
    // text
    {"Text",l_Text},{"TextDisabled",l_TextDisabled},{"TextWrapped",l_TextWrapped},
    {"LabelText",l_LabelText},{"BulletText",l_BulletText},{"SeparatorText",l_SeparatorText},
    {"TextColored",l_TextColored},
    // buttons
    {"Button",l_Button},{"SmallButton",l_SmallButton},{"InvisibleButton",l_InvisibleButton},
    {"ArrowButton",l_ArrowButton},{"Bullet",l_Bullet},
    {"Checkbox",l_Checkbox},{"RadioButton",l_RadioButton},{"ProgressBar",l_ProgressBar},
    // sliders/drags
    {"SliderFloat",l_SliderFloat},{"SliderInt",l_SliderInt},
    {"DragFloat",l_DragFloat},{"DragInt",l_DragInt},
    // input
    {"InputText",l_InputText},{"InputTextMultiline",l_InputTextMultiline},
    {"InputInt",l_InputInt},{"InputFloat",l_InputFloat},
    // color
    {"ColorEdit3",l_ColorEdit3},{"ColorEdit4",l_ColorEdit4},
    // combo/list
    {"BeginCombo",l_BeginCombo},{"EndCombo",l_EndCombo},{"Combo",l_Combo},
    {"Selectable",l_Selectable},{"BeginListBox",l_BeginListBox},{"EndListBox",l_EndListBox},
    // trees/tabs
    {"TreeNode",l_TreeNode},{"TreePop",l_TreePop},{"CollapsingHeader",l_CollapsingHeader},
    {"SetNextItemOpen",l_SetNextItemOpen},
    {"BeginTabBar",l_BeginTabBar},{"EndTabBar",l_EndTabBar},
    {"BeginTabItem",l_BeginTabItem},{"EndTabItem",l_EndTabItem},
    // tables
    {"BeginTable",l_BeginTable},{"EndTable",l_EndTable},{"TableNextRow",l_TableNextRow},
    {"TableNextColumn",l_TableNextColumn},{"TableSetColumnIndex",l_TableSetColumnIndex},
    {"TableSetupColumn",l_TableSetupColumn},{"TableSetupScrollFreeze",l_TableSetupScrollFreeze},
    {"TableHeadersRow",l_TableHeadersRow},
    // menus/popups/tooltips
    {"BeginMenuBar",l_BeginMenuBar},{"EndMenuBar",l_EndMenuBar},
    {"BeginMainMenuBar",l_BeginMainMenuBar},{"EndMainMenuBar",l_EndMainMenuBar},
    {"BeginMenu",l_BeginMenu},{"EndMenu",l_EndMenu},{"MenuItem",l_MenuItem},
    {"OpenPopup",l_OpenPopup},{"BeginPopup",l_BeginPopup},{"BeginPopupModal",l_BeginPopupModal},
    {"BeginPopupContextItem",l_BeginPopupContextItem},{"EndPopup",l_EndPopup},
    {"CloseCurrentPopup",l_CloseCurrentPopup},
    {"BeginTooltip",l_BeginTooltip},{"EndTooltip",l_EndTooltip},{"SetTooltip",l_SetTooltip},
    // layout
    {"Separator",l_Separator},{"SameLine",l_SameLine},{"NewLine",l_NewLine},{"Spacing",l_Spacing},
    {"Dummy",l_Dummy},{"Indent",l_Indent},{"Unindent",l_Unindent},
    {"BeginGroup",l_BeginGroup},{"EndGroup",l_EndGroup},
    {"SetNextItemWidth",l_SetNextItemWidth},{"PushItemWidth",l_PushItemWidth},{"PopItemWidth",l_PopItemWidth},
    {"GetContentRegionAvail",l_GetContentRegionAvail},
    {"GetCursorPosX",l_GetCursorPosX},{"GetCursorPosY",l_GetCursorPosY},
    {"SetCursorPosX",l_SetCursorPosX},{"SetCursorPosY",l_SetCursorPosY},
    // id/item/style
    {"PushID",l_PushID},{"PopID",l_PopID},
    {"IsItemHovered",l_IsItemHovered},{"IsItemClicked",l_IsItemClicked},
    {"IsItemActive",l_IsItemActive},{"IsItemFocused",l_IsItemFocused},
    {"SetItemDefaultFocus",l_SetItemDefaultFocus},
    {"BeginDisabled",l_BeginDisabled},{"EndDisabled",l_EndDisabled},
    {"PushStyleColor",l_PushStyleColor},{"PopStyleColor",l_PopStyleColor},
    {"StyleColorsDark",l_StyleColorsDark},{"StyleColorsLight",l_StyleColorsLight},
    {"StyleColorsClassic",l_StyleColorsClassic},
    // plots/misc
    {"PlotLines",l_PlotLines},{"GetFrameRate",l_GetFrameRate},
    {"ShowDemoWindow",l_ShowDemoWindow},{"ShowMetricsWindow",l_ShowMetricsWindow},
    {"SetScale",l_SetScale},{"GetScale",l_GetScale},{"GetVersion",l_GetVersion},
    {"SetOverlayMode",l_SetOverlayMode},
    // vector sliders/drags, color picker
    {"SliderFloat2",l_SliderFloat2},{"SliderFloat3",l_SliderFloat3},{"SliderFloat4",l_SliderFloat4},
    {"DragFloat2",l_DragFloat2},{"DragFloat3",l_DragFloat3},
    {"ColorPicker4",l_ColorPicker4},{"ColorButton",l_ColorButton},
    // window/mouse/clipboard
    {"IsWindowHovered",l_IsWindowHovered},{"IsWindowFocused",l_IsWindowFocused},
    {"SetWindowFontScale",l_SetWindowFontScale},
    {"GetMousePos",l_GetMousePos},{"IsMouseClicked",l_IsMouseClicked},{"IsMouseDown",l_IsMouseDown},
    {"SetClipboardText",l_SetClipboardText},{"GetClipboardText",l_GetClipboardText},
    // forms
    {"CreateForm",l_CreateForm},
    {nullptr,nullptr}
};

static const luaL_Reg kFormMethods[] = {
    {"Show",l_form_Show},{"Hide",l_form_Hide},{"Toggle",l_form_Toggle},{"Destroy",l_form_Destroy},
    {nullptr,nullptr}
};

// Push some commonly-used enum constants onto the ImGui table.
static void registerEnums(lua_State* L, int tbl) {
    struct { const char* n; lua_Integer v; } E[] = {
        {"WindowFlags_NoTitleBar",     ImGuiWindowFlags_NoTitleBar},
        {"WindowFlags_NoResize",       ImGuiWindowFlags_NoResize},
        {"WindowFlags_NoMove",         ImGuiWindowFlags_NoMove},
        {"WindowFlags_NoCollapse",     ImGuiWindowFlags_NoCollapse},
        {"WindowFlags_AlwaysAutoResize",ImGuiWindowFlags_AlwaysAutoResize},
        {"WindowFlags_MenuBar",        ImGuiWindowFlags_MenuBar},
        {"WindowFlags_NoScrollbar",    ImGuiWindowFlags_NoScrollbar},
        {"WindowFlags_NoBackground",   ImGuiWindowFlags_NoBackground},
        {"TableFlags_Borders",         ImGuiTableFlags_Borders},
        {"TableFlags_RowBg",           ImGuiTableFlags_RowBg},
        {"TableFlags_Resizable",       ImGuiTableFlags_Resizable},
        {"TableFlags_ScrollY",         ImGuiTableFlags_ScrollY},
        {"TableFlags_Sortable",        ImGuiTableFlags_Sortable},
        {"TableFlags_SizingStretchProp",ImGuiTableFlags_SizingStretchProp},
        {"InputTextFlags_Password",    ImGuiInputTextFlags_Password},
        {"InputTextFlags_ReadOnly",    ImGuiInputTextFlags_ReadOnly},
        {"InputTextFlags_EnterReturnsTrue",ImGuiInputTextFlags_EnterReturnsTrue},
        {"TreeNodeFlags_DefaultOpen",  ImGuiTreeNodeFlags_DefaultOpen},
        {"Cond_FirstUseEver",          ImGuiCond_FirstUseEver},
        {"Cond_Once",                  ImGuiCond_Once},
        {"Cond_Always",                ImGuiCond_Always},
        {"Col_Text",                   ImGuiCol_Text},
        {"Col_Button",                 ImGuiCol_Button},
        {"Col_WindowBg",               ImGuiCol_WindowBg},
        {"Dir_Left",ImGuiDir_Left},{"Dir_Right",ImGuiDir_Right},{"Dir_Up",ImGuiDir_Up},{"Dir_Down",ImGuiDir_Down},
        {nullptr,0}
    };
    for (int i=0; E[i].n; i++) { lua_pushinteger(L, E[i].v); lua_setfield(L, tbl, E[i].n); }
}

void RegisterImGui(lua_State* L) {
    // Form metatable
    luaL_newmetatable(L, FORM_MT);
    lua_newtable(L);
    luaL_setfuncs(L, kFormMethods, 0);
    lua_setfield(L, -2, "__index");
    lua_pop(L, 1);

    // forms array in registry
    lua_newtable(L);
    lua_setfield(L, LUA_REGISTRYINDEX, FORMS_KEY);

    // global ImGui table
    lua_newtable(L);
    luaL_setfuncs(L, kImGuiFuncs, 0);
    registerEnums(L, lua_gettop(L));
    lua_setglobal(L, "ImGui");
}

} // namespace CEImGui
