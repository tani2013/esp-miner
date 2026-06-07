---
name: bitaxe-gamma-601
description: Build, flash, configure, start up, and troubleshoot the Bitaxe Gamma 601 (BM1370 ASIC) running esp-miner firmware. Use when the user mentions Bitaxe Gamma, board version 601, BM1370, or asks to build/flash/configure/bring up/self-test/debug a Gamma 601 miner.
---

# Bitaxe Gamma 601 (BM1370) — esp-miner Skill

Workflow and reference for the **Bitaxe Gamma 601** board running the
[esp-miner](https://github.com/bitaxeorg/ESP-Miner) firmware. Use the exact
commands and values below — they are taken from this repository, not memory.

## Board facts (source: `config-601.cvs`, `readme.md`, `AGENTS.md`)

| Property        | Value                                            |
|-----------------|--------------------------------------------------|
| Device model    | `gamma`                                           |
| Board version   | `601`                                             |
| ASIC model      | `BM1370`                                           |
| Default freq    | `525` MHz (`asicfrequency`)                       |
| Default voltage | `1150` mV (`asicvoltage`)                          |
| MCU             | ESP32-S3-WROOM-1 **N16R8 only** (16MB flash, 8MB Octal PSRAM) |
| Self-test       | enabled by default (`selftest=1`)                 |
| Auto fan        | on by default (`autofanspeed=1`)                  |
| Config file     | `config-601.cvs`                                  |
| Toolchain       | ESP-IDF **v5.5.1**, Node.js v22+ (for Axe-OS)     |

> Only the N16R8 module works. Modules without PSRAM or with Quad SPI PSRAM
> will not run the normal firmware.

## 1. Build

```bash
# Source the ESP-IDF environment (path may differ on the user's machine)
. ~/esp/v5.5.1/esp-idf/export.sh

# Build firmware (also builds & compresses the Axe-OS frontend into www.bin)
idf.py build

# Produce a single merged image for flashing
./merge_bin.sh ./esp-miner-merged.bin
```

To bake the 601 config into the merged image, generate `config.bin` first,
then merge with `-c`:

```bash
# config.bin is built from config-601.cvs by the NVS partition gen tool,
# or simply flash the config separately with bitaxetool (see step 2).
./merge_bin.sh -c ./esp-miner-merged.bin
```

Build only the frontend (Angular, in `main/http_server/axe-os`):

```bash
cd main/http_server/axe-os
npm install
npm run build      # or: npm run start   (local dev server)
```

## 2. Flash

Use **bitaxetool** (pin the versions — newer esptool breaks it):

```bash
pip install bitaxetool==0.6.1   # locks esptool to 4.9.0

# Flash firmware + the 601 config together (config overrides baked-in values)
bitaxetool --config ./config-601.cvs --firmware ./esp-miner-merged.bin

# Flash only firmware
bitaxetool --firmware ./esp-miner-merged.bin

# Flash only the 601 config (NVS)
bitaxetool --config ./config-601.cvs
```

For a factory reset, flash the official factory image matching board 601:
`esp-miner-factory-601-vX.Y.Z.bin` (from the GitHub releases page).

### Flash layout (esp32s3, dio, 16MB, 80MHz — from `merge_bin.sh`)

| Binary                  | Address    |
|-------------------------|------------|
| bootloader              | `0x0`      |
| partition-table         | `0x8000`   |
| config (NVS)            | `0x9000`   |
| esp-miner.bin           | `0x10000`  |
| www.bin (Axe-OS)        | `0x410000` |
| ota_data_initial.bin    | `0xf10000` |

> Some Bitaxe units can't connect directly to a USB-C port — use a USB-A
> adapter. Run bitaxetool **outside** any dev container (USB device access).

## 3. First startup / bring-up

1. Power the board via the **barrel connector** (not USB alone).
2. After flashing, wait until esptool prints `Leaving...`, then press **RESET**.
3. The **self-test** runs (`selftest=1`). It can take a moment.
4. Once self-test passes, press **RESET** again to start mining.
5. On first boot with no Wi-Fi creds, the device opens a **`bitaxe` AP** —
   join it and configure Wi-Fi + pool, or pre-set them in `config-601.cvs`
   (`wifissid`, `wifipass`, `stratumurl`, `stratumport`, `stratumuser`).
6. On the LAN, reach the UI at `http://bitaxe` (mDNS) or `http://<IP>`.

## 4. Configure (key `config-601.cvs` fields)

- `stratumurl` / `stratumport` / `stratumuser` / `stratumpass` — primary pool
- `fbstratum*` — fallback pool
- `asicfrequency` (525) / `asicvoltage` (1150) — performance; raising both
  increases hashrate and heat. Overclock UI is locked unless you append
  `?oc` to the settings-tab URL. **No extra cooling → risk of overheating/damage.**
- `autofanspeed` (1) / `fanspeed` — cooling
- `overheat_mode` — thermal protection
- `selftest` — run self-test on next boot

## 5. AxeOS API (port 80) — verify a running 601

```bash
curl http://<IP>/api/system/info        # version, hashrate, temps, board info
curl http://<IP>/api/system/asic        # ASIC settings (expect BM1370)
curl -X POST http://<IP>/api/system/restart
curl -X POST http://<IP>/api/system/identify   # blink/say hi
# Update fan via PATCH:
curl -X PATCH http://<IP>/api/system -H "Content-Type: application/json" \
     -d '{"fanspeed":"100"}'
```

Full spec: `main/http_server/openapi.yaml`. Recovery UI: `http://<IP>/recovery`.

## 6. Tests

```bash
idf.py build test                          # firmware C unit tests (test/)
cd main/http_server/axe-os && npm run test:ci   # Axe-OS (headless Chrome)
```

## 7. Troubleshooting checklist

- **No hashrate / won't mine:** check Wi-Fi router — disable **AiProtection**
  and **IoT** modes (common on ASUS / some TP-Link). Verify `stratumurl`/port.
- **Won't boot / brick after `www.bin` update:** use `http://<IP>/recovery`.
- **Flash fails / device not found:** run bitaxetool outside any container;
  try a USB-A adapter; confirm bitaxetool 0.6.1 + esptool 4.9.0.
- **Boots but crashes / odd PSRAM errors:** confirm the module is N16R8.
- **Overheats:** ensure heatsink + fan; lower `asicfrequency`/`asicvoltage`;
  confirm `autofanspeed=1` and `overheat_mode` behavior.
- **Self-test fails:** re-seat ASIC power (barrel connector), check voltage
  rail; reflash factory image for board 601.

## When applying changes to this repo

Match existing conventions. After firmware changes, build with `idf.py build`,
re-merge, and (if possible) self-test on real 601 hardware before declaring done.
Never invent config keys — the authoritative set lives in `config-601.cvs`.
