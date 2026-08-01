---
name: sizeof
description: Report SwitchTester firmware flash and static-RAM usage for a build config — used / free / total per linker region, with % used. Use when the user asks "how big is the build", "sizeof", "flash usage", "how much ROM/RAM is left", "how much headroom", or after a change to check consumption. Defaults to Debug. Wraps scripts/sizeof.ps1.
---

# sizeof

## What it does

Prints a condensed size report for a built configuration — one line per memory
region declared in the linker script:

```
SwitchTester sizeof  [Debug]
RAM        used     3,664 B (    3.6 KiB)   free   143,792 B (  140.4 KiB)   total   147,456 B (  144.0 KiB)     2.5% used
FLASH      used    37,128 B (   36.3 KiB)   free   485,112 B (  473.7 KiB)   total   522,240 B (  510.0 KiB)     7.1% used
NVM_FLASH  used       512 B (    0.5 KiB)   free     1,536 B (    1.5 KiB)   total     2,048 B (    2.0 KiB)    25.0% used
```

Regions and their sizes come from the build's `.map`, not from hardcoded
constants, so the report stays correct if `STM32G0B1RETX_FLASH.ld` changes — and
any region added later shows up automatically.

## How each figure is derived

- **FLASH used** = `text + data` from `arm-none-eabi-size`: the sum of the
  flash-loadable section sizes, including the flash-resident initialiser copy of
  `.data`, and excluding NOLOAD sections (never programmed). The `.bin` on disk
  can be a few bytes larger because `objcopy` spans inter-section alignment
  padding — currently 4 B between `.isr_vector` and `.text`.
- **Every other region** (RAM, NVM_FLASH, …) is measured by summing the sections
  from `size -A` whose address falls inside that region.

### Why not "data + bss" for RAM

Because it is **wrong on this project**. GNU `size` counts every NOLOAD section
into the Berkeley `bss` figure regardless of where it actually lives, and
SwitchTester's NVM pool (`.nvmdata`, 512 B) sits in flash at `0x0807F800`:

```
.bss 1,452  +  ._user_heap_stack 2,048  +  .nvmdata 512  =  4,012  ← Berkeley "bss"
```

Attributing sections by address instead gives the true RAM figure of 3,664 B and
reports the NVM pool against its own region, where it belongs.

Note RAM "used" includes `._user_heap_stack` — the linker's
`_Min_Heap_Size + _Min_Stack_Size` reserve. That is genuinely allocated RAM, but
it is a floor, not a measurement of real runtime stack depth.

## Steps

1. Pick `<Config>` — Debug | Release. **Default: Debug**.
2. Run: `powershell -ExecutionPolicy Bypass -File scripts/sizeof.ps1 <Config>`
3. Surface the report.

## Requires

- A build of `<Config>` must already exist (`<Config>/SwitchTester.elf` **and**
  `.map`). If missing, the script exits 1 pointing at `scripts\build.ps1` — run
  that first, then re-run.
- `arm-none-eabi-size.exe` — resolved via `$env:STM32_SIZE` → PATH → the CubeIDE
  toolchain plugin under `C:\STM32\STM32CubeIDE` (auto-discovered; it is not on
  PATH on this bench).

## Exit codes

- `0` report printed · `1` no ELF/.map for the config (build first) ·
  `3` size tool missing, or section/region parse failure.

## Notes

- Read-only — no build, no flash, no device access. Safe to run anytime.
- To size a config other than the one last built, build that config first.
- Ported from the ST3074 mirror project 2026-08-01. The region-by-address
  attribution is a SwitchTester-specific fix; the original used `data + bss`,
  which the reserved NVM sector would have inflated.
- No `.sh` port — this bench is PowerShell-primary.
