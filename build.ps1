# build.ps1 — compile CEImGui.dll with MSVC (no IDE needed).
$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $MyInvocation.MyCommand.Path
Set-Location $root

$vsw = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$vsroot = & $vsw -latest -prerelease -property installationPath
if (-not $vsroot) { $vsroot = "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools" }
$vcvars = Join-Path $vsroot "VC\Auxiliary\Build\vcvars64.bat"
if (-not (Test-Path $vcvars)) { throw "vcvars64.bat not found at $vcvars" }

New-Item -ItemType Directory -Force -Path "$root\build","$root\dist" | Out-Null

$srcs = @(
  "src\ce_imgui.cpp",
  "src\renderer.cpp",
  "src\imgui_bindings.cpp",
  "lib\imgui\imgui.cpp",
  "lib\imgui\imgui_draw.cpp",
  "lib\imgui\imgui_tables.cpp",
  "lib\imgui\imgui_widgets.cpp",
  "lib\imgui\imgui_demo.cpp",
  "lib\imgui\backends\imgui_impl_win32.cpp",
  "lib\imgui\backends\imgui_impl_dx11.cpp"
) -join " "

$defs = "/DWIN32 /D_WINDOWS /DNDEBUG /DUNICODE /D_UNICODE " +
        "/DWINVER=0x0A00 /D_WIN32_WINNT=0x0A00 /DNTDDI_VERSION=0x0A000002"
$incs = "/Isrc /Ilib\imgui /Ilib\imgui\backends /Ithird_party\lua"
$libs = "third_party\lua\lua53-64.lib d3d11.lib dxgi.lib dcomp.lib dwmapi.lib user32.lib gdi32.lib"

$cl = "cl /nologo /std:c++17 /O2 /MT /EHsc /GS- /W3 $defs $incs " +
      "/Fobuild\ /Fdbuild\ $srcs " +
      "/link /DLL /OUT:dist\CEImGui.dll $libs"

$bat = @"
@echo off
call "$vcvars" >nul 2>nul
$cl
exit /b %ERRORLEVEL%
"@
$tmp = Join-Path $env:TEMP "ceimgui_build.bat"
Set-Content -Path $tmp -Value $bat -Encoding ascii

Write-Host "Building CEImGui.dll ..." -ForegroundColor Cyan
$ErrorActionPreference = "Continue"
& cmd /c $tmp 2>&1
$code = $LASTEXITCODE
if ($code -eq 0 -and (Test-Path "$root\dist\CEImGui.dll")) {
    Write-Host "OK -> dist\CEImGui.dll" -ForegroundColor Green
} else {
    Write-Host "BUILD FAILED (exit $code)" -ForegroundColor Red
}
exit $code
