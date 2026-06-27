# setup.ps1 — fetch dependencies needed to build CEImGui.dll.
#   1. Clone Dear ImGui (docking branch) into lib/imgui
#   2. Download Lua 5.3 headers into third_party/lua
#   3. Generate a Lua import library from your Cheat Engine's lua53-64.dll
#
# Run once after cloning the repo, then run build.ps1.
param(
    [string]$CheatEngineDir = "C:\Program Files\Cheat Engine"
)
$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $MyInvocation.MyCommand.Path
Set-Location $root

# --- 1. Dear ImGui (docking branch) ---
if (-not (Test-Path "$root\lib\imgui\imgui.h")) {
    Write-Host "Cloning Dear ImGui (docking)..." -ForegroundColor Cyan
    git clone --depth 1 --branch docking https://github.com/ocornut/imgui.git "$root\lib\imgui"
} else { Write-Host "Dear ImGui already present." -ForegroundColor DarkGray }

# --- 2. Lua 5.3 headers ---
New-Item -ItemType Directory -Force -Path "$root\third_party\lua" | Out-Null
foreach ($f in @("lua.h","luaconf.h","lauxlib.h","lualib.h")) {
    $dest = "$root\third_party\lua\$f"
    if (-not (Test-Path $dest)) {
        Write-Host "Downloading $f ..." -ForegroundColor Cyan
        Invoke-WebRequest -UseBasicParsing "https://raw.githubusercontent.com/lua/lua/v5.3.6/$f" -OutFile $dest
    }
}

# --- 3. Import library from CE's lua53-64.dll ---
$dll = Join-Path $CheatEngineDir "lua53-64.dll"
if (-not (Test-Path $dll)) { throw "lua53-64.dll not found in $CheatEngineDir. Pass -CheatEngineDir <path>." }

$vsw = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$vsroot = & $vsw -latest -prerelease -property installationPath
if (-not $vsroot) { $vsroot = "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools" }
$vcvars = Join-Path $vsroot "VC\Auxiliary\Build\vcvars64.bat"

$luaDir = "$root\third_party\lua"
$exp = "$luaDir\lua53-64.exports.txt"
cmd /c "call `"$vcvars`" >nul 2>nul && dumpbin /exports `"$dll`"" | Out-File -Encoding ascii $exp
$names = Get-Content $exp | ForEach-Object {
    if ($_ -match '^\s*\d+\s+[0-9A-Fa-f]+\s+[0-9A-Fa-f]+\s+([A-Za-z_]\w*)\s*$') { $matches[1] }
} | Where-Object { $_ -match '^(lua|luaL|luaopen)' } | Sort-Object -Unique
$def = "LIBRARY lua53-64`r`nEXPORTS`r`n" + (($names | ForEach-Object { "    $_" }) -join "`r`n") + "`r`n"
Set-Content -Path "$luaDir\lua53-64.def" -Value $def -Encoding ascii
cmd /c "call `"$vcvars`" >nul 2>nul && lib /nologo /def:`"$luaDir\lua53-64.def`" /machine:x64 /out:`"$luaDir\lua53-64.lib`""

Write-Host "Setup complete. Now run: .\build.ps1" -ForegroundColor Green
