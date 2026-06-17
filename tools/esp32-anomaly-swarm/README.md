# ESP32 Anomaly Swarm — 25 collaborating Bitcoin-network watchers

A swarm of ESP32-WROOM-32 boards. Each board is an **agent** that subscribes to
your Bitcoin Core node's **ZMQ** feed and hunts for **one real network anomaly**.
Agents collaborate over a **UDP swarm bus**: every finding is broadcast on the
LAN, so all agents — and a **coordinator** agent — see the collective picture and
escalate when several criticals correlate.

> **Honest scope.** This finds REAL anomalies — chain reorgs, network stalls,
> fee/spam spikes, dust storms, empty/near-full blocks, dropped notifications.
> It does **not** and **cannot** find a block or shortcut proof-of-work: SHA-256
> has no exploitable pattern, and ZMQ is just an event feed. This is genuine
> network intelligence and security monitoring, not magic. Output is the on-board
> LED (severity blink patterns) + JSON on serial + the UDP bus.
>
> Written to the ZMTP spec but **must be validated against your real bitcoind** —
> the serial monitor logs every handshake step.

## 1. Enable ZMQ on the node (all topics on one port)

`bitcoin.conf`:
```
zmqpubhashblock=tcp://0.0.0.0:28332
zmqpubhashtx=tcp://0.0.0.0:28332
zmqpubrawblock=tcp://0.0.0.0:28332
zmqpubrawtx=tcp://0.0.0.0:28332
zmqpubsequence=tcp://0.0.0.0:28332
```
Restart, then `bitcoin-cli getzmqnotifications` should list all five on `:28332`.
The `sequence` topic is what makes **reorg detection** possible (it emits block
connect/`C` and disconnect/`D` events).

## 2. Flash each board (Arduino IDE)

Board: **ESP32 Dev Module**, Serial @ 115200. In `esp32_anomaly_swarm.ino` set
once: `WIFI_SSID`, `WIFI_PASS`, `ZMQ_HOST` (node IP). Then per board set:

- `AGENT_ID` — unique `1..25`
- `NODE_ROLE` — the anomaly this agent hunts

## 3. Agent roles

| Role | Topic | Detects |
|------|-------|---------|
| `ROLE_REORG_WATCH` | sequence | **Chain reorg** (block disconnected) — critical |
| `ROLE_FAST_BLOCK` | hashblock | Block found unusually fast (< 120 s) |
| `ROLE_STALL_WATCH` | hashblock | No block for > 30 min (network/node stall) |
| `ROLE_MEMPOOL_FLOOD` | hashtx | Tx rate > 900/min (spam/flood) |
| `ROLE_MEMPOOL_DROUGHT` | hashtx | Tx rate < 3/min (node maybe isolated) — critical |
| `ROLE_WHALE_TX` | rawtx | Very large transaction (> 50 kB) |
| `ROLE_DUST_STORM` | rawtx | Burst of tiny txs (dust attack pattern) |
| `ROLE_BIG_BLOCK` | rawblock | Near-full block (> 3.9 MB) |
| `ROLE_EMPTY_BLOCK` | rawblock | Empty/near-empty block |
| `ROLE_SEQ_GAP` | hashtx | Gaps in the publisher sequence (dropped notifications) |
| `ROLE_COORDINATOR` | — | Aggregates the swarm bus; alarms on **correlated** criticals |

## 4. Suggested allocation for 25 boards

Redundancy + coverage (reorgs and stalls are the highest-value, so double them):

| Count | Role |
|-------|------|
| 1 | `ROLE_COORDINATOR` (agent 1) |
| 3 | `ROLE_REORG_WATCH` |
| 3 | `ROLE_STALL_WATCH` |
| 3 | `ROLE_FAST_BLOCK` |
| 3 | `ROLE_MEMPOOL_FLOOD` |
| 2 | `ROLE_MEMPOOL_DROUGHT` |
| 3 | `ROLE_WHALE_TX` |
| 2 | `ROLE_DUST_STORM` |
| 2 | `ROLE_BIG_BLOCK` |
| 2 | `ROLE_EMPTY_BLOCK` |
| 1 | `ROLE_SEQ_GAP` |

(25 total. Adjust freely — duplicates give redundancy and faster reaction.)

## 5. How they collaborate

- Each agent broadcasts findings as JSON on UDP port `28900`:
  `{"agent":7,"role":"reorg_watch","sev":3,"detail":"CHAIN REORG ..."}`
- Every agent (and the coordinator) receives the whole bus, so the swarm shares
  one picture.
- The **coordinator** escalates when ≥2 critical (`sev:3`) alerts land within
  15 s — e.g. a reorg + a drought together — with a combined alarm.

## 6. Severity → LED

- sev 1 (info): single blink
- sev 2 (warning): 4 blinks
- sev 3 (critical): long 12-blink alarm (coordinator double-alarms on correlation)

## 7. Honest conclusion

This is a unique, real use of 25 agents: distributed, collaborative anomaly
detection on your own node. It protects and informs your operation (you'd know
instantly if your solo-mined block got orphaned by a reorg, or if your node went
quiet). It does not change mining odds — nothing can — but it is genuine,
working intelligence rather than a false promise.
