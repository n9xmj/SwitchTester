# Portable APIs — where the conventions live

**The source of truth is `G0B1_Skeleton`, not this repo.**

    C:\STM32\CubeSource\G0B1_Skeleton\Docs\planning\portable-apis-strategy.md
    C:\STM32\CubeSource\G0B1_Skeleton\Docs\planning\improvements-backlog.md
    C:\STM32\CubeSource\G0B1_Skeleton\Docs\planning\logging-api-plan.md

Skeleton is the canonical base for the vendored APIs; SwitchTester receives them by
back-port. Do not maintain a second copy of the conventions here — they will drift, which
is the exact failure the vendoring model exists to prevent.

## What this repo currently carries

| Module | Directory | State |
|---|---|---|
| `logging` | `App/logging/` | migrated 2026-08-09, verbosity levels, bench-verified |
| `uart_stream` | `App/uart_stream/` | renamed from `uart-stream/` 2026-08-09 |
| `automation_console` | `App/automation_console/` | renamed from `automation-console/` 2026-08-09 |

Port files (edit these, never the module directories):

| Module | Config header | Port source |
|---|---|---|
| `logging` | `App/Inc/logging_config.h` | `App/Src/logging_port.c` |
| `uart_stream` | *(none yet — uses `device_config.h`)* | `App/Src/uart_stream_target.c` |
| `automation_console` | *(none yet — uses `device_config.h`)* | `App/automation_console/automation_commands.c` |

## Migration status

Logging is done here. `nvmparams` is the next portable API to be restructured, and this
repo is the intended home for the **W25Q128 SPI flash driver** that will exercise its
pluggable storage-driver layer — the core chip driver from `LED_Strip_Controller_G474`
only, not the partition or VFS layers. Rationale is in Skeleton's
`improvements-backlog.md` under item 3.

## Note on the older plan documents

The completed decision logs in this directory (`automation-console-plan.md`,
`uart-stream-integration-plan.md`, `switch-cycling-plan.md`) still mention
`debug_config.h` and the old hyphenated directory names in their bodies. Those are
records of decisions as they were made, not statements of the current layout, and are
deliberately left as written.
