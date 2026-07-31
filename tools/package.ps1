# Builds everything and assembles dist\driver_asvr + driver_asvr.zip:
#   driver_asvr\
#     README.txt
#     Config.exe
#     asvr\        <- the SteamVR driver, registered by Config.exe

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

Write-Host "== Building driver + monitor (MSBuild) =="
& $msbuild "$root\driver_asvr.sln" /p:Configuration=Release /p:Platform=x64 /m /v:m /nologo
if ($LASTEXITCODE -ne 0) { Fail "driver build failed" }

Write-Host "== Building Config (CMake) =="
# No -G: the pinned "Visual Studio 17 2022" generator breaks on machines with a newer VS
& $cmake -S "$root\config_tool" -B "$root\build\config_tool" -A x64
if ($LASTEXITCODE -ne 0) { Fail "cmake configure failed" }
& $cmake --build "$root\build\config_tool" --config Release
if ($LASTEXITCODE -ne 0) { Fail "Config build failed" }

Write-Host "== Assembling package =="
$dist = "$root\dist\driver_asvr"
if (Test-Path $dist) { Remove-Item $dist -Recurse -Force }
New-Item -ItemType Directory -Force "$dist\asvr\bin\win64" | Out-Null

Copy-Item "$root\packaging\README.txt" $dist
Copy-Item "$root\build\config_tool\Release\Config.exe" $dist
Copy-Item "$root\bin\driver.vrdrivermanifest" "$dist\asvr\"
Copy-Item "$root\bin\win64\driver_asvr.dll" "$dist\asvr\bin\win64\"
Copy-Item "$root\bin\win64\asvr_monitor.exe" "$dist\asvr\bin\win64\"
Copy-Item "$root\bin\win64\asvr_ctl.exe" "$dist\asvr\bin\win64\"
Copy-Item "$root\vendor\openvr\bin\win64\openvr_api.dll" "$dist\asvr\bin\win64\"
Copy-Item "$root\resources" "$dist\asvr\resources" -Recurse

Compress-Archive -Path $dist -DestinationPath "$root\dist\driver_asvr.zip" -Force
Write-Host "== Done: $root\dist\driver_asvr.zip ==" -ForegroundColor Green
