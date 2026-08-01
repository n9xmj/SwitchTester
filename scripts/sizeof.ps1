# sizeof.ps1 -- report SwitchTester firmware flash + RAM usage for a build config.
#
# Usage:
#   scripts\sizeof.ps1            # Debug (default)
#   scripts\sizeof.ps1 Release
#
# Reports used / free / total for every memory region declared in the linker
# script, taken from the build's .map -- so the report stays correct if the
# linker script changes. Nothing is hardcoded.
#
# Definitions:
#   FLASH used = text + data from arm-none-eabi-size, i.e. the sum of the
#     flash-loadable section sizes. This includes the flash-resident initialiser
#     copy of .data, and correctly EXCLUDES NOLOAD sections, which are never
#     programmed. The .bin on disk can be a few bytes larger because objcopy
#     spans inter-section alignment padding (currently 4 B between .isr_vector
#     and .text) -- 37,128 reported vs a 37,132-byte .bin.
#   Other regions (RAM, NVM_FLASH, ...) are measured by summing the sections
#     whose load address falls inside the region, read from `size -A`.
#
#     Do NOT use the Berkeley "data + bss" figure for RAM on this project:
#     GNU size counts every NOLOAD section as bss regardless of where it lives,
#     so the flash-resident .nvmdata pool (512 B at 0x0807F800) lands in the bss
#     total and would inflate the RAM figure. Attributing sections by address
#     avoids that entirely.
#
#     Note RAM "used" therefore includes ._user_heap_stack -- the linker's
#     _Min_Heap_Size + _Min_Stack_Size reserve. That is genuinely allocated RAM,
#     but it is a floor, not a measurement of actual runtime stack depth.

param(
    [Parameter(Position=0, Mandatory=$false)]
    [ValidateSet("Debug", "Release")]
    [string]$Config = "Debug"
)

$ErrorActionPreference = "Stop"

$projectName = "SwitchTester"
$repoRoot    = Split-Path $PSScriptRoot -Parent
$elf         = Join-Path $repoRoot "$Config\$projectName.elf"
$map         = Join-Path $repoRoot "$Config\$projectName.map"

if (-not (Test-Path $elf)) {
    Write-Error "No ELF for '$Config' at:`n  $elf`nBuild it first:  scripts\build.ps1 $Config"
    exit 1
}
if (-not (Test-Path $map)) {
    Write-Error "No .map for '$Config' at:`n  $map"
    exit 1
}

# Locate arm-none-eabi-size: env override > PATH > CubeIDE toolchain plugin.
function Find-SizeTool {
    if ($env:STM32_SIZE -and (Test-Path $env:STM32_SIZE)) { return $env:STM32_SIZE }
    $onPath = Get-Command arm-none-eabi-size -ErrorAction SilentlyContinue
    if ($onPath) { return $onPath.Source }
    foreach ($root in @("C:\STM32\STM32CubeIDE", "C:\ST", "${env:ProgramFiles}\STMicroelectronics")) {
        if (-not (Test-Path $root)) { continue }
        $hit = Get-ChildItem -Path $root -Recurse -Filter "arm-none-eabi-size.exe" -ErrorAction SilentlyContinue |
               Select-Object -First 1
        if ($hit) { return $hit.FullName }
    }
    return $null
}

$sizeTool = Find-SizeTool
if (-not $sizeTool) {
    Write-Error "arm-none-eabi-size.exe not found. Set `$env:STM32_SIZE to its full path."
    exit 3
}

# --- Berkeley totals: text + data == the flash-loadable image ----------------
$berkeley = & $sizeTool $elf
$dataLine = ($berkeley | Select-String -Pattern '^\s*\d+\s+\d+\s+\d+').Line
if (-not $dataLine) { Write-Error "Could not parse arm-none-eabi-size output."; exit 3 }
$nums = ($dataLine -split '\s+') | Where-Object { $_ -match '^\d+$' }
$flashImage = [int64]$nums[0] + [int64]$nums[1]      # text + data

# --- Per-section sizes and load addresses -----------------------------------
$sections = @()
foreach ($line in (& $sizeTool -A $elf)) {
    if ($line -match '^\s*(\.\S+)\s+(\d+)\s+(\d+)\s*$') {
        $secSize = [int64]$matches[2]
        $secAddr = [int64]$matches[3]
        if (($secSize -gt 0) -and ($secAddr -gt 0)) {
            $sections += [pscustomobject]@{ Name = $matches[1]; Size = $secSize; Addr = $secAddr }
        }
    }
}
if ($sections.Count -eq 0) { Write-Error "Could not parse section table from '$sizeTool -A'."; exit 3 }

# --- Memory regions from the map's Memory Configuration block ----------------
$regions = @()
$inConfig = $false
foreach ($line in Get-Content $map) {
    if ($line -match '^Memory Configuration')  { $inConfig = $true;  continue }
    if ($inConfig -and ($line -match '^Linker script')) { break }
    if (-not $inConfig) { continue }
    if ($line -match '^\s*(\S+)\s+0x([0-9a-fA-F]+)\s+0x([0-9a-fA-F]+)') {
        $name = $matches[1]
        if ($name -eq '*default*') { continue }
        $regions += [pscustomobject]@{
            Name   = $name
            Origin = [Convert]::ToInt64($matches[2], 16)
            Length = [Convert]::ToInt64($matches[3], 16)
        }
    }
}
if ($regions.Count -eq 0) { Write-Error "Could not read memory regions from $map."; exit 3 }

# --- Report ------------------------------------------------------------------
function Fmt([int64]$bytes) { "{0,9:N0} B ({1,7:N1} KiB)" -f $bytes, ($bytes / 1024.0) }
function Pct([int64]$used, [int64]$total) {
    if ($total -le 0) { return "    -" }
    "{0,5:N1}%" -f (100.0 * $used / $total)
}

Write-Host ""
Write-Host "$projectName sizeof  [$Config]" -ForegroundColor Cyan

foreach ($region in $regions) {
    if ($region.Name -eq 'FLASH') {
        # The loadable image, not an address sum -- NOLOAD sections are never
        # programmed and the .data initialiser copy has no VMA in flash.
        $used = $flashImage
    }
    else {
        $regionEnd = $region.Origin + $region.Length
        $used = ($sections |
                 Where-Object { ($_.Addr -ge $region.Origin) -and ($_.Addr -lt $regionEnd) } |
                 Measure-Object -Property Size -Sum).Sum
        if (-not $used) { $used = [int64]0 }
    }

    $free = $region.Length - $used
    Write-Host ("{0,-10} used {1}   free {2}   total {3}   {4} used" -f `
        $region.Name, (Fmt $used), (Fmt $free), (Fmt $region.Length), (Pct $used $region.Length))
}

Write-Host ""
