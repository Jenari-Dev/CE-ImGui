# build_overlay.ps1 — compile ImGuiOverlay.dll (injected in-game DX12 overlay).
$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $MyInvocation.MyCommand.Path
Set-Location $root

$vsw = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$vsroot = & $vsw -latest -prerelease -property installationPath
if (-not $vsroot) { $vsroot = "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools" }
$vcvars = Join-Path $vsroot "VC\Auxiliary\Build\vcvars64.bat"
if (-not (Test-Path $vcvars)) { throw "vcvars64.bat not found at $vcvars" }

New-Item -ItemType Directory -Force -Path "$root\build\overlay","$root\overlay\dist" | Out-Null

$cpp = @(
  "overlay\src\overlay.cpp",
  "lib\imgui\imgui.cpp",
  "lib\imgui\imgui_draw.cpp",
  "lib\imgui\imgui_tables.cpp",
  "lib\imgui\imgui_widgets.cpp",
  "lib\imgui\imgui_demo.cpp",
  "lib\imgui\backends\imgui_impl_win32.cpp",
  "lib\imgui\backends\imgui_impl_dx12.cpp"
) -join " "

$c = @(
  "lib\minhook\src\buffer.c",
  "lib\minhook\src\hook.c",
  "lib\minhook\src\trampoline.c",
  "lib\minhook\src\hde\hde32.c",
  "lib\minhook\src\hde\hde64.c"
) -join " "

$defs = "/DWIN32 /D_WINDOWS /DNDEBUG /DUNICODE /D_UNICODE /DWINVER=0x0A00 /D_WIN32_WINNT=0x0A00"
$incs = "/Ilib\imgui /Ilib\imgui\backends /Ilib\minhook\include"
$libs = "d3d12.lib dxgi.lib user32.lib gdi32.lib"

$cl = "cl /nologo /O2 /MT /EHsc /W3 $defs $incs /Fobuild\overlay\ /Fdbuild\overlay\ " +
      "$cpp $c /link /DLL /OUT:overlay\dist\ImGuiOverlay.dll $libs"

$bat = "@echo off`r`ncall `"$vcvars`" >nul 2>nul`r`n$cl`r`nexit /b %ERRORLEVEL%"
$tmp = Join-Path $env:TEMP "overlay_build.bat"
Set-Content -Path $tmp -Value $bat -Encoding ascii

Write-Host "Building ImGuiOverlay.dll ..." -ForegroundColor Cyan
$ErrorActionPreference = "Continue"
& cmd /c $tmp 2>&1
if ($LASTEXITCODE -eq 0 -and (Test-Path "$root\overlay\dist\ImGuiOverlay.dll")) {
    Write-Host "OK -> overlay\dist\ImGuiOverlay.dll" -ForegroundColor Green
} else {
    Write-Host "BUILD FAILED (exit $LASTEXITCODE)" -ForegroundColor Red
}
exit $LASTEXITCODE
