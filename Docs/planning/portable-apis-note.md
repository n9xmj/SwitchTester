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
| `menusystem` | `App/menusystem/` | reconciled 2026-08-14 to LED_Strip's canonical naming (`x_type`/`c_key`/`pfn_`), Doxygen'd, Phase-2 option-bitfield; byte-identical across all three |

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

`menusystem` was vendored 2026-08-13 (packaging/organization) and **reconciled 2026-08-14**:
LED_Strip's fuller `x_type`/`c_key`/`pfn_` naming is now THE canonical, the module is
Doxygen'd in the `uart_stream` house style, and the per-item option flags collapsed into one
bitfielded union byte (`.b_no_newline`/`.b_not_implemented`, or `.u8_options = MOPT_*`). The
canonical was built in LED_Strip, copied byte-identical to SwitchTester + Skeleton, and each
project's menu **definitions** renamed to the canonical members. All three build 0/0;
SwitchTester was bench-verified (hidden-return + `[At top-level menu]` + submenu ESC). Full
history in Skeleton's `improvements-backlog.md`, item 5.

## Note on the older plan documents

The completed decision logs in this directory (`automation-console-plan.md`,
`uart-stream-integration-plan.md`, `switch-cycling-plan.md`) still mention
`debug_config.h` and the old hyphenated directory names in their bodies. Those are
records of decisions as they were made, not statements of the current layout, and are
deliberately left as written.
