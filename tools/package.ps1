# Builds everything and assembles dist\driver_asvr + driver_asvr.zip:
#   driver_asvr\
#     README.txt
#     asvr_monitor.exe   <- the ASVR window: the user runs this (Install and all
#     openvr_api.dll        settings live in it); the driver also finds it here
#     asvr\              <- the SteamVR driver, registered by the window's Install
#       driver.vrdrivermanifest
#       bin\win64\       driver_asvr.dll, asvr_ctl.exe
#       resources\

$ErrorActionPreference = "Stop"
$root = Split-Path $PSScriptRoot

function Fail($message) {
    Write-Host "ERROR: $message" -ForegroundColor Red
    exit 1
}

# Locate MSBuild
$msbuild = $null
$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
if (Test-Path $vswhere) {
    $msbuild = & $vswhere -latest -requires Microsoft.Component.MSBuild -find "MSBuild\**\Bin\MSBuild.exe" | Select-Object -First 1
}
if (-not $msbuild) { $msbuild = (Get-Command msbuild -ErrorAction SilentlyContinue).Source }
if (-not $msbuild) { Fail "MSBuild not found (install Visual Studio 2022 or newer)." }

# Locate CMake (VS-bundled is fine)
$cmake = (Get-Command cmake -ErrorAction SilentlyContinue).Source
if (-not $cmake -and (Test-Path $vswhere)) {
    $vsroot = & $vswhere -latest -property installationPath | Select-Object -First 1
    if ($vsroot) { $cmake = Join-Path $vsroot "Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" }
}
if (-not $cmake -or -not (Test-Path $cmake)) { Fail "CMake not found (install it or the VS2022 CMake component)." }

Write-Host "== Building driver + asvr_ctl (MSBuild) =="
& $msbuild "$root\driver_asvr.sln" /p:Configuration=Release /p:Platform=x64 /m /v:m /nologo
if ($LASTEXITCODE -ne 0) { Fail "driver build failed" }

Write-Host "== Building asvr_monitor (CMake) =="
# No -G: the pinned "Visual Studio 17 2022" generator breaks on machines with a newer VS.
# Its CMakeLists writes bin\win64\asvr_monitor.exe and copies openvr_api.dll next to it
& $cmake -S "$root\asvr_monitor" -B "$root\build\asvr_monitor" -A x64
if ($LASTEXITCODE -ne 0) { Fail "cmake configure failed" }
& $cmake --build "$root\build\asvr_monitor" --config Release
if ($LASTEXITCODE -ne 0) { Fail "asvr_monitor build failed" }

Write-Host "== Assembling package =="
$dist = "$root\dist\driver_asvr"
# A running SteamVR keeps the shipped driver DLL loaded (this dist folder is
# usually the registered driver), and a half-cleared folder is worse than none
if (Get-Process vrserver -ErrorAction SilentlyContinue) { Fail "SteamVR is running; quit it before packaging (driver_asvr.dll is locked)." }
# Clear the contents rather than the folder itself: a shell or an asvr_monitor
# started from it would otherwise block the delete
if (Test-Path $dist) { Get-ChildItem $dist -Force | Remove-Item -Recurse -Force }
New-Item -ItemType Directory -Force "$dist\asvr\bin\win64" | Out-Null

Copy-Item "$root\packaging\README.txt" $dist
Copy-Item "$root\bin\win64\asvr_monitor.exe" $dist
Copy-Item "$root\vendor\openvr\bin\win64\openvr_api.dll" $dist
Copy-Item "$root\bin\driver.vrdrivermanifest" "$dist\asvr\"
Copy-Item "$root\bin\win64\driver_asvr.dll" "$dist\asvr\bin\win64\"
Copy-Item "$root\bin\win64\asvr_ctl.exe" "$dist\asvr\bin\win64\"
Copy-Item "$root\resources" "$dist\asvr\resources" -Recurse

Compress-Archive -Path $dist -DestinationPath "$root\dist\driver_asvr.zip" -Force
Write-Host "== Done: $root\dist\driver_asvr.zip ==" -ForegroundColor Green
