# SwitchTester — Design Notes

> Status: **application main body not yet implemented.** Peripherals are wired in
> CubeMX but no App code drives them yet. This doc is the working design sketch —
> **partial and in places stale**; firm up the open questions before/while coding.
> Last touched 2026-07-26.

## Concept

Drive 4 "switch" outputs and sense their response on 4 "sense" inputs — a bench
tool for exercising/validating external switching hardware.

## Pin map

| Function | Net label | Pin | Peripheral | Notes |
|---|---|---|---|---|
| Switch out A | SWITCH_A | PC4  | TIM2_CH1 | 32-bit timer, OC |
| Switch out B | SWITCH_B | PC5  | TIM2_CH2 | |
| Switch out C | SWITCH_C | PB10 | TIM2_CH3 | |
| Switch out D | SWITCH_D | PB11 | TIM2_CH4 | |
| Sense A | SENSE_A | PA1 | COMP1 (+) | threshold = DAC1_CH1 (adjustable) |
| Sense B | SENSE_B | PB4 | COMP2 (+) | threshold = DAC1_CH2 (adjustable) |
| Sense C | SENSE_C | PB0 | COMP3 (+) | threshold = 1.2×VREFINT (fixed) |
| Sense D | SENSE_D | PA0 | ADC1_IN0 | analog read; ADC also samples VREFINT |
| DAC ref 1 | — | PA4 | DAC1_CH1 | ext (PA4) + internal → COMP1 ref |
| DAC ref 2 | — | — | DAC1_CH2 | internal only → COMP2 ref |
| LED | NUCLEO_LED | PA5 | GPIO out | |
| Button | NUCLEO_BUTTON | PC13 | EXTI falling | |
| Console | DEBUG_TX/RX | PA2/PA3 | USART2 | 921600 |

## Switch outputs — TIM2 (32-bit)

TIM2 chosen for **32-bit resolution** (long + precise timing). Current config:
PSC 0, ARR 0xFFFFFFFF, internal clock, all 4 channels `OCMode = TIM_OCMODE_TIMING`,
Pulse 0. Note: `OCMODE_TIMING` leaves the output **pins frozen** (no signal); the
final timer-driven waveform scheme is still open (see TODO).

**Bench-test drive approach (decided):** the direct-drive test functions
**reconfigure the SWITCH pins to plain GPIO output** (HAL/LL — `MODE = OUTPUT`
instead of the AF2/TIM2 alternate function) and drive them directly in software.
When the final timer-driven cycle capability lands, the pins get switched **back
to ALTFUNC (AF2 / TIM2)** at cycle-run start. So each SWITCH pin toggles between
GPIO-output (manual / idle) and AF2 (timer-driven run) as needed.

## Sense front-end — DAC-referenced comparators + ADC

- **DAC1** sets programmable comparator thresholds: CH1 → COMP1(−), CH2 → COMP2(−).
- **COMP1/2/3** do fast threshold detection on SENSE_A/B/C. COMP3 uses a fixed
  1.2×VREFINT reference. Currently `TriggerMode NONE` and **not started** — decide
  polled vs EXTI/interrupt.
- **ADC1** (SENSE_D / IN0) for precise analog readback; samples VREFINT for VDDA
  calibration (absolute voltage).

## Open questions / TODO

- [ ] TIM2 **final** timer-driven waveform scheme (PWM / toggle / forced /
      compare-IRQ). For bench tests the pins are driven as plain GPIO (reconfig
      OUTPUT ↔ AF2); the timer-driven cycle scheme itself is still TBD.
- [ ] ADC channel set — currently just IN0 + VREFINT (was 0/1/6/7 in an earlier
      session); confirm final set.
- [ ] Comparators — polled vs interrupt/EXTI; start-up sequencing with DAC refs.
- [ ] Verify the TIM14 1 ms periodic-service rate on the bench (a commented-out
      `printf` in `JOB_PERIODIC` was the check — to be restored).
- [ ] Overall app structure: how switch drive + sense tie into the job queue /
      menu / main loop.

## Next-session goals

1. **Debug-menu tests to exercise the switch outputs** (bench-test external
   switching HW) — direct-drive each SWITCH_A–D on command via GPIO-output
   reconfig (see *Switch outputs* above); no timer needed for this phase.
2. **Plan the full SwitchTester implementation** — discuss drive + sense design and
   app structure before writing the main body.
