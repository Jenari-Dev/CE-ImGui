-- 03_embed_cheat_table.lua — render Cheat Engine's ACTUAL address list inside
-- an ImGui window, so you never have to tab back to the CE main table.
--
-- This is the headline use-case: it reads CE's live memory records
-- (getAddressList) and draws each one as a row with an Active checkbox, the
-- description, and an editable Value field. Toggling/editing here drives CE's
-- real records — the ImGui window IS your cheat table.

local ImGui = require("CEImGui")

local form = ImGui.CreateForm("Cheat Table", 620, 460)
form.ToggleKey = 0x2D   -- INSERT

-- Per-row scratch buffers for value editing (keyed by record ID).
local editBuf = {}

local function drawRecord(rec)
    if rec == nil then return end
    ImGui.TableNextRow()

    -- Column 1: Active checkbox (group headers have no value to freeze)
    ImGui.TableNextColumn()
    ImGui.PushID(rec.ID)
    if not rec.IsGroupHeader then
        local changed, active = ImGui.Checkbox("", rec.Active)
        if changed then rec.Active = active end
    end

    -- Column 2: Description
    ImGui.TableNextColumn()
    if rec.IsGroupHeader then
        ImGui.TextColored(rec.Description, 0.6, 0.8, 1.0)
    else
        ImGui.Text(rec.Description or "")
    end

    -- Column 3: Address
    ImGui.TableNextColumn()
    ImGui.TextDisabled(rec.IsGroupHeader and "" or tostring(rec.Address))

    -- Column 4: Value (editable)
    ImGui.TableNextColumn()
    if not rec.IsGroupHeader then
        local cur = tostring(rec.Value)
        if editBuf[rec.ID] == nil then editBuf[rec.ID] = cur end
        ImGui.SetNextItemWidth(-1)
        local changed, txt = ImGui.InputText("##v", editBuf[rec.ID], 64,
            ImGui.InputTextFlags_EnterReturnsTrue)
        editBuf[rec.ID] = txt
        if changed then
            rec.Value = txt                 -- writes to the process
            editBuf[rec.ID] = tostring(rec.Value)
        end
    end

    ImGui.PopID()
end

form.OnRender = function()
    local al = getAddressList()
    ImGui.Text(string.format("%d records   |   %.0f FPS", al.Count, ImGui.GetFrameRate()))
    ImGui.SameLine()
    if ImGui.Button("Refresh values") then editBuf = {} end
    ImGui.Separator()

    local F = ImGui.TableFlags_Borders | ImGui.TableFlags_RowBg
            | ImGui.TableFlags_Resizable | ImGui.TableFlags_ScrollY
    if ImGui.BeginTable("celist", 4, F) then
        ImGui.TableSetupScrollFreeze(0, 1)
        ImGui.TableSetupColumn("On")
        ImGui.TableSetupColumn("Description")
        ImGui.TableSetupColumn("Address")
        ImGui.TableSetupColumn("Value")
        ImGui.TableHeadersRow()

        -- Draw every record once (flat). Group headers appear inline; you can
        -- later add indentation by inspecting rec.Parent if you want a tree.
        for i = 0, al.Count - 1 do
            drawRecord(al[i])
        end
        ImGui.EndTable()
    end
end
