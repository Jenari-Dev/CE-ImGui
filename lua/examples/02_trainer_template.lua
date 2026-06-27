-- 02_trainer_template.lua — a tabbed trainer skeleton.
--
-- This is the structure to copy for a real trainer: tabs for categories, each
-- cheat a checkbox or slider whose callback drives CE (autoAssemble / writeX).
-- Replace the placeholder bodies with your own AOB scans + scripts.

local ImGui = require("CEImGui")

local form = ImGui.CreateForm("Game Trainer", 480, 420)
form.ToggleKey = 0x2D   -- INSERT

-- trainer state
local s = {
    godmode   = false,
    onehit    = false,
    speed     = 1.0,
    jump      = 1.0,
    infammo   = false,
    money     = 0,
}

-- Example: enable/disable an Auto Assembler script by name. In a real trainer
-- you'd keep your AA scripts in a table and toggle them here.
local function toggleScript(enabled, script)
    -- autoAssemble(script, enabled)   -- uncomment with your script text
end

form.OnRender = function()
    local ch
    if ImGui.BeginTabBar("tabs") then

        if ImGui.BeginTabItem("Combat") then
            ch, s.godmode = ImGui.Checkbox("God Mode", s.godmode)
            if ch then toggleScript(s.godmode, "[ENABLE]\n//...\n[DISABLE]\n//...") end

            ch, s.onehit = ImGui.Checkbox("One-Hit Kill", s.onehit)
            if ch then toggleScript(s.onehit, "") end

            ch, s.infammo = ImGui.Checkbox("Infinite Ammo", s.infammo)
            ImGui.EndTabItem()
        end

        if ImGui.BeginTabItem("Movement") then
            ch, s.speed = ImGui.SliderFloat("Move Speed", s.speed, 0.1, 10.0, "%.1fx")
            -- if ch then writeFloat(speedAddr, s.speed) end
            ch, s.jump  = ImGui.SliderFloat("Jump Height", s.jump, 0.1, 10.0, "%.1fx")
            ImGui.EndTabItem()
        end

        if ImGui.BeginTabItem("Player") then
            ch, s.money = ImGui.InputInt("Money", s.money, 100, 10000)
            ImGui.SameLine()
            if ImGui.Button("Set") then
                -- writeInteger(moneyAddr, s.money)
            end
            ImGui.EndTabItem()
        end

        ImGui.EndTabBar()
    end

    ImGui.Separator()
    ImGui.TextColored("Press INSERT to toggle this window", 0.6, 0.6, 0.6)
end
