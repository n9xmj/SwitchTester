# build.ps1 -- headless STM32CubeIDE build for SwitchTester (Nucleo-G0B1RE)
#
# Usage:
#   scripts\build.ps1                 # Debug (default, clean build)
#   scripts\build.ps1 Release
#   scripts\build.ps1 Debug --incremental
#
# Locates stm32cubeidec.exe via $env:STM32CUBEIDE or common install paths.
# Imports the project into a throwaway temp workspace so the build never
# touches your interactive CubeIDE workspace. Artifacts land in <Config>\.
#
# Adapted from the LED_Strip_Controller_G474 bench scripts. No signing /
# bootloader steps -- this is a plain Nucleo dev image.

if ($args -match '^-?-h(elp)?$') {
    @"
Usage: scripts\build.ps1 [CONFIG] [OPTIONS]

  CONFIG            Debug (default) or Release

Options:
  --clean           Force a clean build (default behavior)
  --incremental     Request an incremental build
  --help, -h        Show this help

Locates stm32cubeidec.exe via `$env:STM32CUBEIDE or common install paths.
"@
    exit 0
}

$ErrorActionPreference = "Stop"
$projectName = "SwitchTester"
$repoRoot    = Split-Path $PSScriptRoot -Parent

$Config = "Debug"
$Incremental = $false

# First positional (non-flag) arg is CONFIG if valid
$argIndex = 0
if ($args.Count -gt 0 -and $args[0] -notmatch '^-') {
    if ($args[0] -in @("Debug","Release")) { $Config = $args[0]; $argIndex = 1 }
}
for ($i = $argIndex; $i -lt $args.Count; $i++) {
    switch -Regex ($args[$i]) {
        '^--?clean$'       { break }             # clean is the default
        '^--?incremental$' { $Incremental = $true; break }
        '^--?config$'      { $Config = $args[++$i]; break }
        default { }
    }
}
if ($Config -notin @("Debug","Release")) {
    Write-Host "Invalid config '$Config', defaulting to Debug" -ForegroundColor Yellow
    $Config = "Debug"
}

function Find-LatestCubeIde {
    $roots = @(
        "C:\ST",
        "C:\STM32",
        "${env:ProgramFiles}\STMicroelectronics",
        "${env:ProgramFiles(x86)}\STMicroelectronics"
    )
    $hits = foreach ($root in $roots) {
        if (-not $root -or -not (Test-Path $root)) { continue }
        Get-ChildItem -Path $root -Directory -Filter "STM32CubeIDE_*" -ErrorAction SilentlyContinue
    }
    $fromVer = $hits |
        ForEach-Object {
            $exe = Join-Path $_.FullName "STM32CubeIDE\stm32cubeidec.exe"
            if (Test-Path $exe) {
                $ver = try { [version]($_.Name -replace '^STM32CubeIDE_','') } catch { [version]'0.0.0' }
                [pscustomobject]@{ Version = $ver; Path = $exe }
            }
        } |
        Sort-Object Version -Descending |
        Select-Object -First 1 -ExpandProperty Path
    if ($fromVer) { return $fromVer }

    foreach ($p in @(
            "C:\STM32\STM32CubeIDE\STM32CubeIDE\stm32cubeidec.exe",
            "C:\STM32\STM32CubeIDE\stm32cubeidec.exe"
        )) {
        if (Test-Path $p) { return $p }
    }
    return $null
}

if ($env:STM32CUBEIDE -and (Test-Path $env:STM32CUBEIDE)) {
    $cubeide = $env:STM32CUBEIDE
} else {
    $cubeide = Find-LatestCubeIde
}
if (-not $cubeide -or -not (Test-Path $cubeide)) {
    Write-Error @"
STM32CubeIDE not found. Set the full path to the launcher:
  `$env:STM32CUBEIDE = 'C:\STM32\STM32CubeIDE\STM32CubeIDE\stm32cubeidec.exe'
"@
    exit 3
}

$ts = Get-Date -Format "yyyyMMdd-HHmmss"
$workspace = Join-Path $env:TEMP "switchtester-headless-ws-$ts"
if (-not (Test-Path $workspace)) { New-Item -ItemType Directory -Path $workspace | Out-Null }

$lockFile = Join-Path $workspace ".metadata\.lock"
if (Test-Path $lockFile) {
    for ($i = 0; $i -lt 5; $i++) {
        Remove-Item $lockFile -Force -ErrorAction SilentlyContinue
        if (-not (Test-Path $lockFile)) { break }
        Start-Sleep -Milliseconds (300 * ($i + 1))
    }
}

$buildVerb = if ($Incremental) { "-build" } else { "-cleanBuild" }

Write-Host "=== SwitchTester build ($Config) ===" -ForegroundColor Cyan
Write-Host "Project:   $projectName"
Write-Host "CubeIDE:   $cubeide"
Write-Host "Workspace: $workspace"
Write-Host "Mode:      $(if ($Incremental) {'incremental (-build)'} else {'clean (-cleanBuild)'})"
Write-Host ""

& $cubeide `
    -nosplash `
    -application org.eclipse.cdt.managedbuilder.core.headlessbuild `
    -data $workspace `
    -import "$repoRoot" `
    $buildVerb "$projectName/$Config" `
    -consoleLog

$rc = $LASTEXITCODE
if ($rc -eq 0) {
    Write-Host "Build OK. Artifacts in $Config\" -ForegroundColor Green
    $elf = Join-Path $repoRoot "$Config\${projectName}.elf"
    if (Test-Path $elf) {
        Write-Host "ELF: $elf  ($((Get-Item $elf).LastWriteTime))"
    }
    if (Test-Path $workspace) {
        Remove-Item $workspace -Recurse -Force -ErrorAction SilentlyContinue
    }
} else {
    Write-Error "Build failed (exit $rc)."
    Write-Host "Temp workspace left for inspection: $workspace" -ForegroundColor Yellow
}
exit $rc
