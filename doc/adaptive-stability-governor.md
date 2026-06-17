# Adaptive Stability Governor (ASG)

The Adaptive Stability Governor is a closed-loop controller that runs **on the
device** and keeps the ASIC at the sweet spot between stability and efficiency.
Instead of running a single fixed frequency/voltage forever, the ASG jointly
tunes **frequency** and **core voltage**, continuously tracking the chip's *real*
operating point and following it as conditions change.

## Why it exists

Every ASIC sample is slightly different ("silicon lottery"), and the frequency
and voltage a chip can sustain shift with ambient temperature. A fixed setting is
therefore either:

- **too aggressive** when the room warms up → rising hardware errors, rejected
  shares, wasted power on invalid work; or
- **too conservative** the rest of the time → performance and efficiency left on
  the table.

Most autotuners run on a host PC and react mainly to temperature. The ASG instead
runs in firmware and uses the **live hardware error rate** (`error_percentage`,
derived from the ASIC's own error counters by the hashrate monitor) as its
primary feedback signal — a rising error rate is the *earliest* sign a chip is
being pushed past its stable point, well before shares get rejected.

## What it controls

| Knob | Goal | Behaviour |
|------|------|-----------|
| **Frequency** | stability (safety-first) | Lowered when the chip is unstable or hot; recovered toward the ceiling once it has been healthy at the voltage floor. |
| **Voltage** | efficiency | While healthy and cool, undervolted one small step at a time to find the lowest voltage that still mines cleanly at the current frequency (best J/TH). Restored when instability appears. |

The two loops are arbitrated by priority so they never fight: **stability always
wins**. Instability or heat reduces frequency (and restores voltage margin);
only a calm, healthy, cool chip is allowed to undervolt for efficiency.

## Safety model

Two properties make this safe to run unattended:

> **1. The governor never raises frequency or voltage above the user-configured
> ceilings.** It can only ever be *more* conservative than what you set.
> Frequency is clamped to `[80% … 100%]` of the ceiling and voltage to
> `[90% … 100%]` of the ceiling, so neither can collapse.

> **2. Hang guard.** Undervolting too far can wedge a chip so it stops producing
> shares — and in that state the error rate misleadingly reads ~0% (there is no
> hashrate to be wrong). The governor therefore also watches the ratio of actual
> to expected hashrate; a collapse (< 60% of expected) is treated as strong
> instability and voltage is restored decisively.

The existing hard overheat protection in `power_management_task` is untouched and
still runs underneath all of this.

## How it works

Every `ASG_DECISION_INTERVAL_MS` (15 s) the governor evaluates one decision, in
priority order:

1. **Hashrate collapsed** (< 60% of expected, after a settle window): restore
   voltage fast (+20 mV), ease frequency (−2 MHz).
2. **Critical errors** (> `target × 2.5`): restore voltage fast (+20 mV), drop
   frequency hard (−10 MHz).
3. **Too hot** (chip > 68 °C): drop frequency (−2 MHz). Voltage is *not* raised
   (that would add heat).
4. **Mild errors** (> target): restore one voltage step (+10 mV) if undervolted,
   otherwise ease frequency (−2 MHz).
5. **Healthy and cool**: after ~2 min stable, undervolt one step (−10 mV) toward
   the efficiency floor; once at the voltage floor, recover frequency (+1 MHz)
   toward the ceiling.

## Configuration

Three settings, all exposed via the standard `PATCH /api/system` endpoint and
persisted in NVS:

| REST field | NVS key | Type | Default | Range | Meaning |
|------------|---------|------|---------|-------|---------|
| `asgEnabled` | `asg_enabled` | bool | `true` | 0–1 | master on/off switch |
| `asgVoltageControl` | `asg_vctrl` | bool | `true` | 0–1 | allow voltage (efficiency) control; if off, only frequency is governed |
| `asgErrorTarget` | `asg_err_tgt` | u16 | `2` | 1–10 | desired steady-state HW error rate (%) |

Examples:

```bash
# disable the governor entirely (restores full configured freq + voltage)
curl -X PATCH http://<bitaxe-ip>/api/system -H 'Content-Type: application/json' \
     -d '{"asgEnabled": 0}'

# stability-only mode: govern frequency but never touch voltage
curl -X PATCH http://<bitaxe-ip>/api/system -H 'Content-Type: application/json' \
     -d '{"asgVoltageControl": 0}'

# run tighter (aim for 1% errors)
curl -X PATCH http://<bitaxe-ip>/api/system -H 'Content-Type: application/json' \
     -d '{"asgErrorTarget": 1}'
```

These can also be controlled from the AxeOS web dashboard (ASG panel).

## Telemetry

`GET /api/system/info` gains these additive (backward-compatible) fields:

- `asgTargetFrequency` — the governor's current commanded frequency (MHz)
- `asgTargetVoltage` — the governor's current commanded core voltage (mV)
- `asgEnabled`, `asgVoltageControl`, `asgErrorTarget`

The governor also logs every change over the serial console / log buffer under
the `power_management` tag, e.g.:

```
ASG freq: 525 -> 523 MHz (err 2.43%, ceil 525 MHz, ASIC 64.1C, hr 99%)
ASG volt: 1200 -> 1190 mV (ceil 1200 mV, err 0.12%)
```

## Tuning constants & tests

All control constants live at the top of `main/asg.h` and are covered by the
unit tests in `test/main/asg_test.c`. The control logic in `main/asg.c` is free
of ESP-IDF dependencies so it can be (and is) validated on the host.
