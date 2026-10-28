# SPDX-License-Identifier: AGPL-3.0-only
# Copyright (C) 2026 FtheBand contributors
# Release build on Windows with VS Build Tools 2022 + Ninja. Logs to build.log.
# ASIO is ON by default (SDK auto-fetched). Usage: .\build.ps1 [-NoAsio] [-AsioSdk C:\path\to\asiosdk] [-Vst2Sdk C:\path\to\vst2sdk] [-Clean]
param([switch]$NoAsio, [string]$AsioSdk = "", [string]$Vst2Sdk = "", [switch]$Clean)
$ErrorActionPreference = "Stop"
$root = $PSScriptRoot; $log = Join-Path $root "build.log"; $bld = Join-Path $root "build"
"=== build $(Get-Date) ===" | Out-File $log
$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$vs = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vs) { throw "VS Build Tools 2022 with C++ workload not found" }
$vcvars = Join-Path $vs "VC\Auxiliary\Build\vcvars64.bat"
# Import the vcvars64 environment into this PowerShell session
cmd /c "`"$vcvars`" >nul && set" | ForEach-Object { if ($_ -match '^([^=]+)=(.*)$') { Set-Item "env:$($matches[1])" $matches[2] } }
if ($Clean -and (Test-Path $bld)) { Remove-Item -Recurse -Force $bld }
$cfg = @("-S", $root, "-B", $bld, "-G", "Ninja", "-DCMAKE_BUILD_TYPE=Release")
if ($NoAsio) { $cfg += "-DENABLE_ASIO=OFF" } else { $cfg += "-DENABLE_ASIO=ON"; if ($AsioSdk) { $cfg += "-DASIO_SDK_DIR=$AsioSdk" } }
if ($Vst2Sdk) { $cfg += "-DENABLE_VST2=ON"; $cfg += "-DVST2_SDK_DIR=$Vst2Sdk" } else { $cfg += "-DENABLE_VST2=OFF" }
& cmake @cfg 2>&1 | Tee-Object -FilePath $log -Append
if ($LASTEXITCODE -ne 0) { throw "CMake configure failed - see build.log" }
& cmake --build $bld --config Release 2>&1 | Tee-Object -FilePath $log -Append
if ($LASTEXITCODE -ne 0) { throw "Build failed - see build.log" }
Write-Host "OK: $bld\Ftheband_artefacts\Release\FtheBand.exe"
