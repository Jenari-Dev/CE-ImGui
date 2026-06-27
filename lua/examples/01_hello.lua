-- 01_hello.lua — the smallest possible CE-ImGui script.
-- Run from CE's Lua Engine (Ctrl+Alt+L). Press INSERT to show/hide.

-- If CEImGui.dll isn't in CE's clibs64 folder, point Lua at it:
-- package.cpath = package.cpath .. ";C:\\path\\to\\CE_imGUI\\dist\\?.dll"

local ImGui = require("CEImGui")

local form = ImGui.CreateForm("Hello CE-ImGui", 360, 200)
form.ToggleKey = 0x2D   -- VK_INSERT

local clicks = 0
form.OnRender = function()
    ImGui.Text("Hello from CE-ImGui!")
    ImGui.Text(string.format("Running at %.0f FPS, scale %.2fx",
        ImGui.GetFrameRate(), ImGui.GetScale()))
    ImGui.Separator()
    if ImGui.Button("Click me") then clicks = clicks + 1 end
    ImGui.SameLine()
    ImGui.Text("clicks: " .. clicks)
end
