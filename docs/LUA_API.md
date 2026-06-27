# CE-ImGui — Lua API Reference

Load the module once, then create forms and draw inside their `OnRender`:

```lua
package.cpath = package.cpath .. ";C:\\path\\to\\dist\\?.dll"   -- if not in clibs64
local ImGui = require("CEImGui")
```

`require` returns the same global `ImGui` table either way.

Conventions:
* Functions that edit a value return **`changed, newValue`**. Capture both:
  `local changed, v = ImGui.SliderFloat("x", v, 0, 1)`.
* `Begin*` / `End*` come in pairs. Most `Begin*` return a boolean — only draw the
  contents / call the matching `End*` per Dear ImGui's rules (see each entry).
* Coordinates/sizes are in DPI-scaled pixels. `0` usually means "auto".

---

## Forms

### `ImGui.CreateForm(title [, width, height]) -> form`
Creates a form (a Lua table) and shows the host window. Returns the form.

Form **fields** (read/write):

| Field | Type | Meaning |
|-------|------|---------|
| `Title` | string | Window title |
| `Visible` | bool | Whether it's drawn this frame |
| `Width`, `Height` | number | Initial size (first use) |
| `OnRender` | function | Called every frame; put your ImGui calls here |
| `ToggleKey` | number | Virtual-key code; pressing it flips `Visible` |
| `NoAutoBegin` | bool | If true, CE-ImGui does **not** wrap your callback in `Begin/End` — you call `ImGui.Begin/End` yourself (for multi-window or custom layouts) |

Form **methods**: `form:Show()`, `form:Hide()`, `form:Toggle()`, `form:Destroy()`.

```lua
local f = ImGui.CreateForm("Trainer", 400, 300)
f.ToggleKey = 0x2D            -- VK_INSERT
f.OnRender = function()
    ImGui.Text("Hello from CE-ImGui")
end
```

---

## Windows & layout containers

| Function | Returns | Notes |
|----------|---------|-------|
| `ImGui.Begin(name [, open, flags])` | `visible, open` | If `open` passed, shows a close button; read returned `open`. Always pair with `End`. |
| `ImGui.End()` | | |
| `ImGui.BeginChild(id [, w, h, border, flags])` | `visible` | Scrollable sub-region. Always pair with `EndChild`. |
| `ImGui.EndChild()` | | |
| `ImGui.SetNextWindowSize(w, h [, cond])` | | |
| `ImGui.SetNextWindowPos(x, y [, cond])` | | |
| `ImGui.SetNextWindowCollapsed(bool [, cond])` | | |
| `ImGui.GetWindowWidth()` / `GetWindowHeight()` | number | |

---

## Text

| Function | Notes |
|----------|-------|
| `ImGui.Text(s)` | Plain text |
| `ImGui.TextColored(s, r, g, b [, a])` | Colored (0–1 floats) |
| `ImGui.TextDisabled(s)` | Greyed |
| `ImGui.TextWrapped(s)` | Wraps to window width |
| `ImGui.LabelText(label, value)` | Right-aligned label + value |
| `ImGui.BulletText(s)` | |
| `ImGui.SeparatorText(s)` | Separator with centered label |

---

## Buttons & basic widgets

| Function | Returns |
|----------|---------|
| `ImGui.Button(label [, w, h])` | `clicked` |
| `ImGui.SmallButton(label)` | `clicked` |
| `ImGui.InvisibleButton(id [, w, h])` | `clicked` |
| `ImGui.ArrowButton(id, dir)` | `clicked` (dir = `ImGui.Dir_*`) |
| `ImGui.Bullet()` | |
| `ImGui.Checkbox(label, value)` | `changed, value` |
| `ImGui.RadioButton(label, active)` | `pressed` |
| `ImGui.ProgressBar(fraction [, w, h, overlay])` | |

---

## Sliders, drags, inputs

| Function | Returns |
|----------|---------|
| `ImGui.SliderFloat(label, v, min, max [, fmt])` | `changed, v` |
| `ImGui.SliderInt(label, v, min, max)` | `changed, v` |
| `ImGui.DragFloat(label, v [, speed, min, max, fmt])` | `changed, v` |
| `ImGui.DragInt(label, v [, speed, min, max])` | `changed, v` |
| `ImGui.InputText(label, text [, maxlen, flags])` | `changed, text` |
| `ImGui.InputTextMultiline(label, text [, w, h, maxlen])` | `changed, text` |
| `ImGui.InputInt(label, v [, step, stepfast])` | `changed, v` |
| `ImGui.InputFloat(label, v [, step, stepfast, fmt])` | `changed, v` |

---

## Color

| Function | Returns |
|----------|---------|
| `ImGui.ColorEdit3(label, {r,g,b} [, flags])` | `changed, {r,g,b}` |
| `ImGui.ColorEdit4(label, {r,g,b,a} [, flags])` | `changed, {r,g,b,a}` |

---

## Combo / selectable / list

| Function | Returns | Notes |
|----------|---------|-------|
| `ImGui.Combo(label, current, {items})` | `changed, current` | `current` is **1-based** |
| `ImGui.BeginCombo(label, preview [, flags])` / `ImGui.EndCombo()` | `open` | manual combo |
| `ImGui.Selectable(label [, selected, flags])` | `clicked, selected` | |
| `ImGui.BeginListBox(label [, w, h])` / `ImGui.EndListBox()` | `open` | |

---

## Trees, headers, tabs

| Function | Returns |
|----------|---------|
| `ImGui.TreeNode(label)` / `ImGui.TreePop()` | `open` |
| `ImGui.CollapsingHeader(label [, flags])` | `open` |
| `ImGui.SetNextItemOpen(bool [, cond])` | |
| `ImGui.BeginTabBar(id [, flags])` / `ImGui.EndTabBar()` | `open` |
| `ImGui.BeginTabItem(label)` / `ImGui.EndTabItem()` | `selected` |

```lua
if ImGui.BeginTabBar("tabs") then
    if ImGui.BeginTabItem("Combat") then ImGui.Text("...") ImGui.EndTabItem() end
    if ImGui.BeginTabItem("Player") then ImGui.Text("...") ImGui.EndTabItem() end
    ImGui.EndTabBar()
end
```

---

## Tables

| Function | Returns |
|----------|---------|
| `ImGui.BeginTable(id, columns [, flags, w, h])` | `open` |
| `ImGui.EndTable()` | |
| `ImGui.TableNextRow([flags, minHeight])` | |
| `ImGui.TableNextColumn()` | `visible` |
| `ImGui.TableSetColumnIndex(i)` | `visible` |
| `ImGui.TableSetupColumn(label [, flags, width])` | |
| `ImGui.TableSetupScrollFreeze(cols, rows)` | |
| `ImGui.TableHeadersRow()` | |

```lua
local F = ImGui.TableFlags_Borders | ImGui.TableFlags_RowBg | ImGui.TableFlags_Resizable
if ImGui.BeginTable("addr", 3, F) then
    ImGui.TableSetupColumn("Name"); ImGui.TableSetupColumn("Address"); ImGui.TableSetupColumn("Value")
    ImGui.TableHeadersRow()
    for _, e in ipairs(entries) do
        ImGui.TableNextRow()
        ImGui.TableNextColumn(); ImGui.Text(e.name)
        ImGui.TableNextColumn(); ImGui.Text(e.addr)
        ImGui.TableNextColumn(); ImGui.Text(tostring(readInteger(e.addr)))
    end
    ImGui.EndTable()
end
```

---

## Menus, popups, tooltips

| Function | Returns |
|----------|---------|
| `ImGui.BeginMenuBar()` / `ImGui.EndMenuBar()` | `open` — needs `WindowFlags_MenuBar` on the window |
| `ImGui.BeginMainMenuBar()` / `ImGui.EndMainMenuBar()` | `open` |
| `ImGui.BeginMenu(label [, enabled])` / `ImGui.EndMenu()` | `open` |
| `ImGui.MenuItem(label [, shortcut, selected, enabled])` | `clicked, selected` |
| `ImGui.OpenPopup(id)` | |
| `ImGui.BeginPopup(id [, flags])` / `ImGui.EndPopup()` | `open` |
| `ImGui.BeginPopupModal(id [, flags])` | `visible, open` |
| `ImGui.BeginPopupContextItem([id])` | `open` |
| `ImGui.CloseCurrentPopup()` | |
| `ImGui.BeginTooltip()` / `ImGui.EndTooltip()` | `open` |
| `ImGui.SetTooltip(s)` | |

---

## Spacing & cursor

`ImGui.Separator()`, `ImGui.SameLine([offset, spacing])`, `ImGui.NewLine()`,
`ImGui.Spacing()`, `ImGui.Dummy(w, h)`, `ImGui.Indent([w])`, `ImGui.Unindent([w])`,
`ImGui.BeginGroup()` / `ImGui.EndGroup()`,
`ImGui.SetNextItemWidth(w)`, `ImGui.PushItemWidth(w)` / `ImGui.PopItemWidth()`,
`ImGui.GetContentRegionAvail()` → `w, h`,
`ImGui.GetCursorPosX/Y()`, `ImGui.SetCursorPosX/Y(v)`.

---

## ID stack, item queries, disabling, style

`ImGui.PushID(id)` / `ImGui.PopID()` — `id` may be a string or number.
`ImGui.IsItemHovered()`, `ImGui.IsItemClicked([button])`, `ImGui.IsItemActive()`,
`ImGui.IsItemFocused()`, `ImGui.SetItemDefaultFocus()`.
`ImGui.BeginDisabled([bool])` / `ImGui.EndDisabled()`.
`ImGui.PushStyleColor(colEnum, r, g, b [, a])` / `ImGui.PopStyleColor([count])`.
`ImGui.StyleColorsDark()`, `ImGui.StyleColorsLight()`, `ImGui.StyleColorsClassic()`.

---

## Plots & misc

`ImGui.PlotLines(label, {values} [, min, max, w, h])`.
`ImGui.GetFrameRate()` → fps.
`ImGui.ShowDemoWindow()` — the full Dear ImGui demo (great for exploring).
`ImGui.ShowMetricsWindow()`.
`ImGui.SetScale(x)` / `ImGui.GetScale()` — UI scale (auto-detected from DPI).
`ImGui.GetVersion()` → Dear ImGui version string.

---

## Enum constants

Exposed on the `ImGui` table; combine flags with `|` (bitwise or, Lua 5.3):

* Window: `WindowFlags_NoTitleBar`, `_NoResize`, `_NoMove`, `_NoCollapse`,
  `_AlwaysAutoResize`, `_MenuBar`, `_NoScrollbar`, `_NoBackground`
* Table: `TableFlags_Borders`, `_RowBg`, `_Resizable`, `_ScrollY`, `_Sortable`,
  `_SizingStretchProp`
* Input: `InputTextFlags_Password`, `_ReadOnly`, `_EnterReturnsTrue`
* Tree: `TreeNodeFlags_DefaultOpen`
* Cond: `Cond_FirstUseEver`, `Cond_Once`, `Cond_Always`
* Color id: `Col_Text`, `Col_Button`, `Col_WindowBg`
* Dir: `Dir_Left`, `Dir_Right`, `Dir_Up`, `Dir_Down`

More constants are easy to add — see `registerEnums` in `src/imgui_bindings.cpp`.

---

## Common virtual-key codes (for `ToggleKey`)

`VK_INSERT = 0x2D`, `VK_DELETE = 0x2E`, `VK_HOME = 0x24`, `VK_END = 0x23`,
`VK_F1..F12 = 0x70..0x7B`.
