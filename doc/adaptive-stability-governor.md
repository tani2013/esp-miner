# Adaptive Stability Governor (ASG)

The Adaptive Stability Governor is a closed-loop frequency controller that runs
**on the device** and keeps the ASIC operating just below its instability
threshold. Instead of running a single fixed frequency/voltage forever, the ASG
continuously tracks the chip's *real* stable operating point and follows it as
conditions change.

## Why it exists

Every ASIC sample is slightly different ("silicon lottery"), and the frequency a
chip can sustain shifts with ambient temperature. A fixed frequency is therefore
either:

- **too high** when the room warms up → rising hardware errors, rejected shares,
  wasted power on invalid work; or
- **too low** the rest of the time → performance left on the table.

Most autotuners run on a host PC and react mainly to temperature. The ASG instead
uses the **live hardware error rate** (`error_percentage`, derived from the ASIC's
own error counters by the hashrate monitor) as its primary feedback signal —
because a rising error rate is the *earliest* sign that a chip is being pushed
past its stable point, well before shares start getting rejected.

## Safety model

The single most important property:

> **The governor never raises frequency above the user-configured ceiling.**

It can only *lower* the frequency from the value you set, and slowly recover back
up toward it once the chip is healthy again. This means the ASG can never push
the hardware beyond limits you have already chosen — in the worst case it is
simply more conservative than a fixed setting. Voltage is left untouched at your
configured value; lowering frequency at a fixed voltage *increases* stability
margin.

It also layers cleanly under the existing overheat protection in
`power_management_task` — if the firmware's hard thermal limits trip, that path
still runs exactly as before.

## How it works

Every `ASG_DECISION_INTERVAL_MS` (15 s) the governor evaluates one decision:

| Condition | Action |
|-----------|--------|
| error rate > `target × 2.5` (critical) | drop hard (−10 MHz), reset stability clock |
| error rate > target, or chip warm (> 68 °C) | ease off (−2 MHz), reset stability clock |
| healthy for `8` consecutive windows (~2 min) **and** thermally comfortable | recover (+1 MHz) toward the ceiling |

The target frequency is always clamped to `[80% of ceiling … ceiling]`, so it
can never collapse to nothing. Between decisions the governor holds steady to let
the chip — and the error-rate signal — settle after each change.

## Configuration

Two settings, both exposed via the standard `PATCH /api/system` endpoint and
persisted in NVS:

| REST field | NVS key | Type | Default | Range | Meaning |
|------------|---------|------|---------|-------|---------|
| `asgEnabled` | `asg_enabled` | bool | `true` | 0–1 | master on/off switch |
| `asgErrorTarget` | `asg_err_tgt` | u16 | `2` | 1–10 | desired steady-state HW error rate (%) |

Example — disable the governor:

```bash
curl -X PATCH http://<bitaxe-ip>/api/system -H 'Content-Type: application/json' \
     -d '{"asgEnabled": 0}'
```

Example — run it tighter (aim for 1% errors):

```bash
curl -X PATCH http://<bitaxe-ip>/api/system -H 'Content-Type: application/json' \
     -d '{"asgErrorTarget": 1}'
```

## Telemetry

`GET /api/system/info` gains three additive (backward-compatible) fields:

- `asgTargetFrequency` — the governor's current commanded frequency (MHz)
- `asgEnabled`
- `asgErrorTarget`

The governor also logs every frequency change over the serial console / log
buffer under the `power_management` tag, e.g.:

```
ASG: 525 -> 523 MHz (err 2.43%, target 2%, ceil 525 MHz, ASIC 64.1C)
```

## Tuning constants

All control constants live at the top of `main/asg.h` and are covered by the
unit tests in `test/main/asg_test.c`. The control logic in `main/asg.c` is free
of ESP-IDF dependencies so it can be (and is) validated on the host.
