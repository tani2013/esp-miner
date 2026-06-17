# Solo mining your Bitaxe Gamma 601 to your own Umbrel / Bitcoin Core

This guide points your Bitaxe directly at your **own full node** running on
Umbrel, so you mine solo: **0% pool fees**, the **full block reward** (3.125 BTC
+ fees) if you ever hit a block, your **own coinbase address**, and the lowest
possible latency because the node is on your LAN.

> **Honest note.** Solo mining does not change the odds — Bitcoin is proof-of-work,
> a pure lottery proportional to hashrate. A Gamma at ~1.2 TH/s against a ~700+
> EH/s network has an astronomically small per-block chance. What solo-to-local
> *does* give you is real and worth having: zero fees, full reward, full
> decentralisation, and **zero wasted hashes** (you switch to each new block
> faster than anyone behind a remote pool). The firmware's **Solo Sniper**
> telemetry (below) makes that edge visible.

## 1. Install a solo Stratum server on Umbrel

Bitcoin Core does not speak Stratum directly, so you need a thin solo server in
front of it. On Umbrel the easiest is the **Public Pool** app (a personal solo
pool backed by your own `bitcoind`):

1. Open the **Umbrel App Store** and install **Bitcoin Node** (Bitcoin Core).
   Let it finish the initial block download (IBD) — it must be fully synced.
2. Install **Public Pool** from the App Store. It auto-connects to your local
   Bitcoin Node over RPC/ZMQ.
3. Open the Public Pool app. Note the **Stratum host/port** it shows, typically:
   - Host: your Umbrel's LAN IP, e.g. `192.168.1.50` (or `umbrel.local`)
   - Port: `2018` (check the app — it is shown in its UI)

> Alternative: `ckpool-solo` in "solo" mode works too if you prefer a CLI setup,
> but Public Pool is the one-click path on Umbrel.

## 2. Point the Bitaxe at it (AxeOS)

In the AxeOS web UI → **Settings** → Pool/Stratum:

| Field | Value |
|-------|-------|
| Stratum URL | your Umbrel LAN IP (e.g. `192.168.1.50`) |
| Stratum Port | the Public Pool port (e.g. `2018`) |
| Stratum User | **your own BTC address**`.bitaxe` (e.g. `bc1q...yourwallet.gamma601`) |
| Stratum Password | `x` (anything) |

The part **before** the `.` is the payout address that goes into the coinbase —
use a wallet **you control**. The part after the `.` is just a worker label.

Set the **fallback** pool to a normal solo pool (e.g. a public solo.ckpool) so
the miner keeps working if your node restarts.

Save and reboot. The dashboard should show it connected and accepting shares
against your node.

## 3. Recommended settings for solo

- Keep the **Adaptive Stability Governor** ON (it is by default) so the chip runs
  at its most efficient stable point — efficiency matters more than raw peak when
  you are in it for the long haul.
- A **Balanced** or **Eco** profile (Profit Engine widget) is sensible for 24/7
  solo running; **Turbo** only if you have good cooling.
- Difficulty: Public Pool sets a suitable share difficulty automatically.

## 4. Watch your Solo Sniper edge

The firmware tracks the network block tip and exposes it in
`GET /api/system/info`:

| Field | Meaning |
|-------|---------|
| `blocksSeen` | how many distinct network blocks the miner has switched to this session |
| `blockAgeSeconds` | seconds since the last new block arrived (how fresh the current work is) |

And it logs every switch on the serial console / log buffer:

```
SOLO SNIPER: new block (seen #1423) - switching to fresh work
```

A healthy solo setup shows `blockAgeSeconds` resetting every ~10 minutes (the
average block interval) and `blocksSeen` climbing steadily — proof your Bitaxe is
always working the newest block and never wasting hashes on a solved one.

## 5. If you ever hit a block

Public Pool / your node constructs the block and `bitcoind` broadcasts it; the
full reward goes to the coinbase address you set in step 2. Because it is your
own node and your own template, there is no pool and no fee in between.

Good luck — and remember: the maths is the same for everyone, but your hashes are
clean, free, and entirely yours.
