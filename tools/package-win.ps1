# SPDX-License-Identifier: AGPL-3.0-only
# Copyright (C) 2026 FtheBand contributors
# Builds FtheBand Release (ASIO ON, VST2 OFF) via build.ps1 and makes dist\FtheBand-<Version>-win-x64.zip containing:
#   FtheBand.exe  README.md LICENSE LICENSING.md CREDITS.md THIRD-PARTY-NOTICES.md SOURCE.txt (commit stamped)
#   samples\                base set (assets\samples) + samples.map           (-NoBaseSamples leaves it out)
#   samples\packs\<id>\     every pack of catalog\packs.json: files + pack.map + pack.json + CREDIT.txt (CC0/CC-BY, sha256-verified)
#   samples\CREDITS.txt     credit lines for the bundled packs
#   (no drivers or installers: FlexASIO is downloaded by the app from its official release only on the player's click; ASIO4ALL is only a link)
# ASIO4ALL is NOT bundled (its licence forbids redistribution); the README links to https://asio4all.org.
# Downloads are cached in -PackCache (default %LOCALAPPDATA%\Ftheband-pack-cache) and every file is checked against the sha256 in the catalog / pinned below.
# Usage: .\tools\package-win.ps1 [-Version 0.1.0-beta] [-SkipBuild] [-Clean] [-AsioSdk C:\path\to\asiosdk] [-PackCache D:\cache] [-NoBaseSamples] [-NoPacks]
param([string]$Version = "0.1.0-beta", [switch]$SkipBuild, [switch]$Clean, [string]$AsioSdk = "", [string]$PackCache = "",
      [switch]$NoBaseSamples, [switch]$NoPacks, [string]$Commit = "")
$ErrorActionPreference = "Stop"
$ProgressPreference = "SilentlyContinue"
[Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12
$root = Split-Path -Parent $PSScriptRoot
$exe  = Join-Path $root "build\Ftheband_artefacts\Release\FtheBand.exe"
$dist = Join-Path $root "dist"
$name = "FtheBand-$Version-win-x64"
$zip  = Join-Path $dist "$name.zip"
if (-not $PackCache) { $PackCache = Join-Path $env:LOCALAPPDATA "Ftheband-pack-cache" }
$utf8 = New-Object System.Text.UTF8Encoding($false)

function Get-Verified([string]$Url, [string]$Dest, [string]$Sha) {
    if (Test-Path $Dest) {
        if ((Get-FileHash $Dest -Algorithm SHA256).Hash.ToLower() -eq $Sha.ToLower()) { return $false }   # cache hit
        Remove-Item -Force $Dest
    }
    New-Item -ItemType Directory -Force -Path (Split-Path -Parent $Dest) | Out-Null
    $tmp = "$Dest.partial"
    for ($try = 1; $try -le 4; $try++) {
        try {
            if (Test-Path $tmp) { Remove-Item -Force $tmp }
            Invoke-WebRequest -Uri $Url -OutFile $tmp -UseBasicParsing
            $h = (Get-FileHash $tmp -Algorithm SHA256).Hash.ToLower()
            if ($h -ne $Sha.ToLower()) { throw "sha256 mismatch for $Url (expected $Sha, got $h)" }
            Move-Item -Force $tmp $Dest
            return $true
        } catch {
            if ($_.Exception.Message -like "sha256 mismatch*") { throw }   # never retry a bad hash
            if ($try -eq 4) { throw "Download failed: $Url : $($_.Exception.Message)" }
            Start-Sleep -Seconds (2 * $try)
        }
    }
}

if (-not $SkipBuild) {
    # build.ps1 defaults: Release, ASIO ON (no -NoAsio), VST2 OFF (no -Vst2Sdk). build.ps1 passes ASIO/VST2 options explicitly on every configure.
    $bargs = @{}
    if ($Clean) { $bargs["Clean"] = $true }
    if ($AsioSdk) { $bargs["AsioSdk"] = $AsioSdk }
    & (Join-Path $root "build.ps1") @bargs
}
foreach ($f in @($exe, "LICENSE", "LICENSING.md", "CREDITS.md", "THIRD-PARTY-NOTICES.md", "README.md", "SOURCE.txt")) {
    $p = if ([IO.Path]::IsPathRooted($f)) { $f } else { Join-Path $root $f }
    if (-not (Test-Path $p)) { throw "Missing $p" }
}

New-Item -ItemType Directory -Force -Path $dist | Out-Null
if (Test-Path $zip) { Remove-Item -Force $zip }
$stage = Join-Path $dist $name
if (Test-Path $stage) { Remove-Item -Recurse -Force $stage }
New-Item -ItemType Directory -Path $stage | Out-Null
Copy-Item $exe $stage
foreach ($f in "LICENSE", "LICENSING.md", "CREDITS.md", "THIRD-PARTY-NOTICES.md", "README.md") { Copy-Item (Join-Path $root $f) $stage }
# SOURCE.txt: stamp the commit this binary was built from (-Commit, else git rev-parse in the tree)
if (-not $Commit) { try { $Commit = (& git -C $root rev-parse HEAD 2>$null | Select-Object -First 1) } catch { $Commit = "" } }
if (-not $Commit -or $Commit -notmatch '^[0-9a-f]{7,40}$') { throw "SOURCE.txt needs the git commit: pass -Commit <hash> (the tree has no .git)" }
$src = [IO.File]::ReadAllText((Join-Path $root "SOURCE.txt"), $utf8)
$src = $src.Replace("Commit:    @COMMIT@", "Commit:    $Commit  (https://github.com/thenotagjesus/ftheband/commit/$Commit)")
[IO.File]::WriteAllText((Join-Path $stage "SOURCE.txt"), $src, $utf8)

# ---- base sample set (assets\samples: drums\ bass\ keys\ + samples.map) -> samples\
$samples = Join-Path $stage "samples"
New-Item -ItemType Directory -Force -Path $samples | Out-Null
if (-not $NoBaseSamples) {
    $base = Join-Path $root "assets\samples"
    if (-not (Test-Path (Join-Path $base "samples.map"))) { throw "assets\samples\samples.map not found. Fetch the base set (see assets\samples.manifest) or pass -NoBaseSamples." }
    Copy-Item (Join-Path $base "*") $samples -Recurse
    Write-Host "base samples: $((Get-ChildItem $samples -Recurse -File | Measure-Object).Count) files"
}

# ---- bundled catalog packs -> samples\packs\<id>\
if (-not $NoPacks) {
    $catalog = Get-Content (Join-Path $root "catalog\packs.json") -Raw -Encoding UTF8 | ConvertFrom-Json
    $credits = New-Object System.Text.StringBuilder
    [void]$credits.AppendLine("Sample packs bundled with FtheBand (all CC0 1.0 unless stated). Files are unmodified upstream samples, verified by SHA-256.")
    [void]$credits.AppendLine("FtheBand's own list and licence evidence: CREDITS.md. Pack-level metadata: packs\<id>\pack.json")
    [void]$credits.AppendLine("")
    [void]$credits.AppendLine("Keys (base set, samples\keys): Wurlitzer EP200 Electric Piano v1.1 by Greg Sullivan (http://www.sullivang.net/), SFZ mapping by kinwie, from https://github.com/sfzinstruments/GregSullivan.E-Pianos (commit 8c3e581acda3594b553948ff0222d4f84a698376). Licensed under Creative Commons Attribution 3.0 Unported, https://creativecommons.org/licenses/by/3.0/ . Used without endorsement by the author.")
    [void]$credits.AppendLine("")
    $nFiles = 0; $nBytes = 0; $nDown = 0
    foreach ($p in $catalog) {
        $id = [string]$p.id
        if ($id -notmatch '^[a-z0-9_-]{1,48}$') { throw "Bad pack id: $id" }
        if ([string]$p.licence -notmatch '^(CC0|CC-BY)') { throw "Pack $id has a licence that is not CC0/CC-BY: $($p.licence)" }
        $prefix = "packs/$id/"
        $outDir = Join-Path $samples "packs\$id"
        New-Item -ItemType Directory -Force -Path $outDir | Out-Null
        $set = switch -Regex (([string]$p.category).ToLower()) { '^(drum|perc)' { "drums" } '^bass' { "bass" } default { "keys" } }
        $map = New-Object System.Text.StringBuilder
        [void]$map.AppendLine("# dest|note|vel|keytrack|set   (generated by tools\package-win.ps1 from catalog\packs.json)")
        foreach ($e in $p.entries) {
            $rel = [string]$e.dest
            if ($rel.StartsWith($prefix)) { $rel = $rel.Substring($prefix.Length) }
            if ($rel -match '(^/)|(\.\.)|[\\:]' -or $rel -notmatch '\.(wav|flac|aif|aiff|ogg)$') { throw "Unsafe dest in catalog: $($e.dest)" }
            $cache = Join-Path $PackCache ("$id\" + $rel.Replace('/', '\'))
            if (Get-Verified ([string]$e.url) $cache ([string]$e.sha256)) { $nDown++ }
            $target = Join-Path $outDir $rel.Replace('/', '\')
            New-Item -ItemType Directory -Force -Path (Split-Path -Parent $target) | Out-Null
            Copy-Item $cache $target
            $kt = if ([int]$e.keytrack -ne 0) { 1 } else { 0 }
            [void]$map.AppendLine("$rel|$([int]$e.root_note)|$([int]$e.velocity)|$kt|$set")
            $nFiles++; $nBytes += (Get-Item $target).Length
        }
        [IO.File]::WriteAllText((Join-Path $outDir "pack.map"), $map.ToString(), $utf8)
        $meta = [ordered]@{ id = $id; name = [string]$p.name; licence = [string]$p.licence; credit = [string]$p.credit; description = "Bundled with FtheBand"
                            size = [int64]$p.size_bytes; sha256 = ""; category = [string]$p.category; licence_url = [string]$p.licence_url; upstream_url = [string]$p.upstream_url }
        [IO.File]::WriteAllText((Join-Path $outDir "pack.json"), ($meta | ConvertTo-Json), $utf8)
        $note = "$($p.name)`r`nCredit: $($p.credit)`r`nLicence: $($p.licence) - $($p.licence_url)`r`nEvidence: $($p.licence_quote)`r`nSource: $($p.upstream_url)`r`n"
        [IO.File]::WriteAllText((Join-Path $outDir "CREDIT.txt"), $note, $utf8)
        [void]$credits.AppendLine("* $($p.name) [$id]: $($p.credit). $($p.licence). $($p.upstream_url)")
        Write-Host ("pack {0}: {1} files" -f $id, @($p.entries).Count)
    }
    [IO.File]::WriteAllText((Join-Path $samples "CREDITS.txt"), $credits.ToString(), $utf8)
    Write-Host ("bundled packs: {0} packs, {1} files, {2:N1} MB ({3} downloaded this run, rest from cache {4})" -f @($catalog).Count, $nFiles, ($nBytes / 1MB), $nDown, $PackCache)
}

# ---- no third-party installers or drivers are bundled: FtheBand downloads FlexASIO from its official release only if the player asks (Setup check); ASIO4ALL is only a link.

# ---- zip (forward-slash entry names, FtheBand.exe at the zip root, no wrapper folder, no ASIO4ALL anywhere)
Add-Type -AssemblyName System.IO.Compression
Add-Type -AssemblyName System.IO.Compression.FileSystem
$fs = [IO.File]::Open($zip, [IO.FileMode]::Create)
$za = New-Object IO.Compression.ZipArchive($fs, [IO.Compression.ZipArchiveMode]::Create)
$stageFull = (Resolve-Path $stage).Path.TrimEnd('\') + '\'
$files = Get-ChildItem $stage -Recurse -File | Sort-Object FullName
foreach ($f in $files) {
    $entryName = $f.FullName.Substring($stageFull.Length).Replace('\', '/')
    [void][IO.Compression.ZipFileExtensions]::CreateEntryFromFile($za, $f.FullName, $entryName, [IO.Compression.CompressionLevel]::Optimal)
}
$za.Dispose(); $fs.Dispose()
if (($files | Where-Object { $_.Name -match 'ASIO4ALL' }).Count -gt 0) { throw "ASIO4ALL must not be in the package" }
Remove-Item -Recurse -Force $stage
$h = (Get-FileHash $zip -Algorithm SHA256).Hash
Write-Host "OK: $zip  ($([math]::Round((Get-Item $zip).Length / 1MB, 1)) MB, $($files.Count) files)"
Write-Host "SHA256 $h"
