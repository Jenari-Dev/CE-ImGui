# CE-ImGui

A **free, open-source** [Dear ImGui](https://github.com/ocornut/imgui) plugin for
[Cheat Engine](https://github.com/cheat-engine/cheat-engine). Build modern,
fully custom trainer and tool UIs in Lua — buttons, sliders, tables, tabs,
plots, the whole ImGui widget set — rendered in their own crisp, DPI-aware
window right alongside Cheat Engine.

It's a community alternative to the paid CEmGui plugin. No game injection: ImGui
renders in its own window driven entirely from CE Lua, so it works with any
process and integrates seamlessly with CE's memory/debugger features.

```lua
local ImGui = require("CEImGui")

local form = ImGui.CreateForm("My Trainer", 420, 320)
form.ToggleKey = 0x2D                       -- INSERT shows/hides the window
local god, speed = false, 1.0

form.OnRender = function()
    local changed
    changed, god = ImGui.Checkbox("God Mode", god)
    if changed then writeBytes(godAddr, god and 0x90 or 0xFF) end   -- call CE APIs directly

    changed, speed = ImGui.SliderFloat("Speed", speed, 0.1, 10.0, "%.1fx")
    if changed then writeFloat(speedAddr, speed) end

    if ImGui.Button("One Punch") then autoAssemble(onePunchScript) end
end
```

## Why this design (and how it differs from the previous attempt)

CE-ImGui is an **immediate-mode** binding: you write an `OnRender` callback that
calls ImGui functions every frame, exactly how Dear ImGui is meant to be used.

* **Full feature set for free.** Every ImGui function is a thin direct binding,
  so tables, plots, docking, popups, trees — everything — is available without
  per-widget C++ plumbing.
* **Render on CE's main thread.** A CE Lua timer ticks the frame, so your
  `OnRender` callbacks can call CE APIs (`readInteger`, `writeFloat`,
  `autoAssemble`, `getAddress`, ...) **directly**, with no thread marshaling.
* **Crisp on 4K / high-DPI.** The window opts into per-monitor DPI awareness and
  auto-scales fonts + style to your monitor. No tiny, no blur. Override with
  `ImGui.SetScale(x)`.
* **Robust loading.** The DLL links against an import library generated from your
  CE's real `lua53-64.dll`, so all `lua_*` / `luaL_*` symbols resolve correctly —
  no fragile runtime symbol loader.

## Requirements

* Cheat Engine 7.x (64-bit) — uses Lua 5.3 / `lua53-64.dll`
* Windows 10/11 with a D3D11-capable GPU (any modern machine)
* To build: Visual Studio 2022+ or VS Build Tools (MSVC + Windows SDK)

## Build

```powershell
git clone <this repo> CE-ImGui
cd CE-ImGui
.\setup.ps1          # clones Dear ImGui, fetches Lua headers, makes the import lib
.\build.ps1          # produces dist\CEImGui.dll
```

If your Cheat Engine isn't at the default path:
`./setup.ps1 -CheatEngineDir "D:\Tools\Cheat Engine"`.

## Install

`require()` looks for the DLL on Lua's C-module search path (`package.cpath`).
Two options:

1. **Drop-in (no admin):** keep `CEImGui.dll` anywhere, and prepend its folder in
   your script:
   ```lua
   package.cpath = package.cpath .. ";C:\\path\\to\\CEImGui\\dist\\?.dll"
   local ImGui = require("CEImGui")
   ```
2. **System-wide:** copy `CEImGui.dll` into Cheat Engine's `clibs64\` folder
   (needs admin to write under `Program Files`). Then just
   `local ImGui = require("CEImGui")`.

Put your trainer script in CE's `autorun\` folder to load it automatically.

## Overlay mode (float over a fullscreen game)

Call `ImGui.SetOverlayMode(true)` **before** creating your first form and the UI
renders in a transparent, always-on-top, click-through window composited over the
screen with DirectComposition — so it floats over a **borderless / windowed-
fullscreen** game (not exclusive fullscreen). No injection, no graphics-API
hooking, nothing for anti-cheat/anti-tamper to object to: it's just your own
window painted on top.

```lua
local ImGui = require("CEImGui")
ImGui.SetOverlayMode(true)
local f = ImGui.CreateForm("Trainer")
f.ToggleKey = 0x2D            -- INSERT
f.OnRender = function() ... end
```

It's click-through whenever no form is visible (input goes to the game) and
captures input while a form is visible — so press your toggle key to flip between
playing and using the menu.

## API

See [docs/LUA_API.md](docs/LUA_API.md) for the full function reference and
[lua/examples/](lua/examples/) for complete trainer scripts. Quick mental model:

* `ImGui.CreateForm(title[, w, h])` → a **form** (a Lua table). Set
  `form.OnRender = function() ... end`; it runs every frame inside its own window.
* Inside `OnRender`, call any `ImGui.*` function. Widgets that return a value
  return `changed, newValue` (idiomatic Lua), e.g.
  `local changed, v = ImGui.SliderInt("HP", v, 0, 100)`.
* Form fields: `Title`, `Visible`, `Width`, `Height`, `ToggleKey` (a VK code),
  `NoAutoBegin` (draw your own `ImGui.Begin/End` instead of the auto window).
* Form methods: `form:Show()`, `form:Hide()`, `form:Toggle()`, `form:Destroy()`.

## Status

Working & verified live in Cheat Engine:
- Immediate-mode ImGui driven from CE Lua, on CE's main thread
- Automatic 4K / high-DPI scaling (crisp, not tiny/blurry)
- Broad binding surface (widgets, tables, tabs, trees, plots, menus, popups…)
- Forms with `OnRender` callbacks and hotkey toggles
- Transparent always-on-top overlay mode (over borderless-fullscreen games)
- CE control panel: scanning, live address list, memory viewer, Lua console

Planned: ImGui multi-viewport, font-customization API, image/texture widgets,
async scanning. An injected DX12 overlay exists under `overlay/` for games
without aggressive anti-tamper (it does not work on Denuvo-protected titles,
which reject injected rendering — use overlay mode instead).

## License

MIT — see [LICENSE](LICENSE). Built on Dear ImGui (MIT) and Lua (MIT). Free to
use, modify, and share. This is exactly the kind of thing that shouldn't be
locked behind a paywall.
