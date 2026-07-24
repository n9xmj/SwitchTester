# flash.ps1 -- flash SwitchTester via STM32_Programmer_CLI (Nucleo-G0B1RE)
#
# Usage:
#   scripts\flash.ps1                        # flash Debug\SwitchTester.elf to the bench ST-Link
#   scripts\flash.ps1 Release
#   scripts\flash.ps1 --stlink-sn 0671FF...  # override the bench SN
#   scripts\flash.ps1 --list                 # list connected ST-Link probes
#
# The target ST-Link SN comes from scripts\bench.defaults.json (override per
# machine with scripts\bench.defaults.local.json). Pinning the SN matters on
# this bench because TWO probes are attached -- the Nucleo's on-board ST-Link
# and a stand-alone STLINK-V3SET -- so auto-select would be ambiguous and
# could program the wrong target.

if ($args -match '^-?-h(elp)?$') {
    Write-Host @'
Usage: scripts\flash.ps1 [CONFIG] [OPTIONS]

  CONFIG            Debug (default) or Release

Options:
  --stlink-sn SN    Target a specific ST-Link serial (default: bench.defaults.json)
  --list            List connected ST-Link probes (SN + board name)
  --help, -h        Show this help

Uses STM32_Programmer_CLI (prefers $env:STM32_PROGRAMMER_CLI).
'@
    exit 0
}

$ErrorActionPreference = "Stop"
$repoRoot = Split-Path $PSScriptRoot -Parent
$projectName = "SwitchTester"

$Config = "Debug"
$StlinkSn = $null
$List = $false

$argIndex = 0
if ($args.Count -gt 0 -and $args[0] -notmatch '^-') {
    if ($args[0] -in @("Debug","Release")) { $Config = $args[0]; $argIndex = 1 }
}
for ($i = $argIndex; $i -lt $args.Count; $i++) {
    switch -Regex ($args[$i]) {
        '^--?stlink-sn$' { $StlinkSn = $args[++$i]; break }
        '^--?list$'      { $List = $true; break }
        default { }
    }
}

$programmer = $env:STM32_PROGRAMMER_CLI
if (-not $programmer -or -not (Test-Path $programmer)) {
    $programmer = "C:\STM32\STM32CubeProgrammer\bin\STM32_Programmer_CLI.exe"
    if (-not (Test-Path $programmer)) {
        Write-Error "STM32_Programmer_CLI not found. Set `$env:STM32_PROGRAMMER_CLI or install STM32CubeProgrammer."
        exit 3
    }
}

if ($List) { & $programmer --list st-link; exit $LASTEXITCODE }

# Resolve the bench ST-Link SN: explicit flag > per-machine override > committed default
function Get-BenchDefault([string]$key) {
    foreach ($f in @("$PSScriptRoot\bench.defaults.local.json", "$PSScriptRoot\bench.defaults.json")) {
        if (Test-Path $f) {
            $j = Get-Content $f -Raw | ConvertFrom-Json
            if (($j.PSObject.Properties.Name -contains $key) -and $j.$key) { return $j.$key }
        }
    }
    return $null
}
if (-not $StlinkSn) { $StlinkSn = Get-BenchDefault 'stlink_sn' }
if (-not $StlinkSn) {
    Write-Error @"
No ST-Link SN selected. Set stlink_sn in scripts\bench.defaults.json,
or pass --stlink-sn SN. List probes with:  scripts\flash.ps1 --list
"@
    exit 3
}

$artifact = Join-Path $repoRoot "$Config\${projectName}.elf"
if (-not (Test-Path $artifact)) {
    Write-Error "Artifact not found: $artifact  (build first: scripts\build.ps1 $Config)"
    exit 2
}

$connect = "port=SWD sn=$StlinkSn"

Write-Host "=== SwitchTester flash ($Config) ===" -ForegroundColor Cyan
Write-Host "Artifact:   $artifact"
Write-Host "ST-Link:    $StlinkSn"
Write-Host "Programmer: $programmer"
Write-Host ""

& $programmer -c $connect -w $artifact -v -rst
$rc = $LASTEXITCODE
if ($rc -eq 0) {
    Write-Host "Flash OK" -ForegroundColor Green
} else {
    Write-Error "Flash failed (exit $rc)"
}
exit $rc
