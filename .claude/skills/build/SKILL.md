---
name: build
description: Build SwitchTester firmware in Debug or Release via headless STM32CubeIDE. Use when the user asks to "build", "compile", "rebuild", "build Debug/Release", or as the first step before flashing or checking size. Defaults to Debug. Wraps scripts/build.ps1.
---

# build

## What it does

Headless STM32CubeIDE managed-build for the chosen configuration, run in a
throwaway temp workspace so it never disturbs an interactive CubeIDE session.
Output goes to `<Config>/`.

## Steps

1. Pick `<Config>` — Debug | Release. **Default: Debug** if unspecified.
2. Run: `powershell -ExecutionPolicy Bypass -File scripts/build.ps1 <Config>`
   Add `--incremental` to skip the clean (default is a clean build).
3. On success, report the artifact path: `<Config>/SwitchTester.elf`
   (`.bin`, `.list` and `.map` land alongside it).
4. On failure, surface the **exact compiler error with its file and line** — do
   not summarize away that information.

## Reading the output

The script prints a `Build OK` banner, then the compile lines, then a
`Build Finished. N errors, N warnings` summary. **The compiler summary is
authoritative.**

Ignore the Eclipse CDT indexer line — it reports things like
`2,155 syntax errors; 2,328 unresolved names` because a fresh temp workspace
hasn't resolved the toolchain headers. That is not a build result and says
nothing about code correctness. If `Build Finished` reports `0 errors,
0 warnings`, the build is clean regardless of what the indexer claims.

## Toolchain location

Resolved via `$env:STM32CUBEIDE`, else a scan of `C:\ST`, `C:\STM32` and
Program Files. On this bench it lands at
`C:\STM32\STM32CubeIDE\STM32CubeIDE\stm32cubeidec.exe` — ST tools live under
`C:\STM32\...`, **not** Program Files.

## Notes

- Build output directories (`Debug/`, `Release/`) are gitignored — never commit.
- Follow with the `sizeof` skill to check flash/RAM headroom, or
  `scripts\flash.ps1 <Config>` to program the board.
- **Flashing hazard:** two ST-Link probes are on this bench. `flash.ps1` pins the
  serial number from `scripts/bench.defaults.json`
  (`0671FF485251667187121242`, the NUCLEO-G0B1RE); the other probe is the DUT
  board and must never be targeted. Run `scripts\flash.ps1 --list` and match both
  the SN and the reported board name if there is any doubt.
- Ported from the ST3074 mirror project 2026-08-01, stripped of that project's
  Test config, image signing and submodule steps.
