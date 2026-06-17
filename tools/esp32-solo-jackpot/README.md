# ESP32 Solo Jackpot Alarm

An ESP32-WROOM-32 that connects to your Bitcoin Core node's ZMQ `rawblock` feed,
parses the **coinbase** of every new block, and checks whether it pays **your
address**. If it does, that block was mined by *you* (true solo) → it fires a
**JACKPOT alarm**: LED + optional buzzer/relay + a broadcast on the swarm bus so
a whole fleet of boards celebrates at once.

> **Honest scope.** This does **not** help find a block — nothing can shortcut
> proof-of-work. It is the unique, real "connection to BTC" for a solo miner: the
> single moment that matters, announced physically the instant it happens. Until
> then it harmlessly logs each block's coinbase so you can watch the parser work.

## 1. Get your scriptPubKey (once)

On the node:
```
bitcoin-cli getaddressinfo <your_payout_btc_address>
```
Copy the `scriptPubKey` hex (a bech32 `bc1q...` address gives `0014<20 bytes>`).
Paste it into `MY_SCRIPTPUBKEY_HEX` in the sketch.

> Use the **same address** you set as the Stratum user on your Bitaxe / in your
> solo pool (CHRONOS / Public Pool), so the coinbase pays it directly.

## 2. Enable rawblock ZMQ

`bitcoin.conf` (and restart):
```
zmqpubrawblock=tcp://0.0.0.0:28332
```
Verify: `bitcoin-cli getzmqnotifications` lists `rawblock`.

## 3. Flash (Arduino IDE)

Board: **ESP32 Dev Module**, Serial @ 115200. Set `WIFI_SSID`, `WIFI_PASS`,
`ZMQ_HOST`, and `MY_SCRIPTPUBKEY_HEX`. Optional: wire an active buzzer to GPIO4
and/or a relay/siren/light to GPIO5 (both safe to leave unconnected — the
on-board LED still alarms).

## 4. What you'll see

Every block (proof the parser works, showing whoever actually won):
```
{"block":42,"coinbaseOut0":"0014ab12...","valueSat":312500000}
```
And the day it's you:
```
################################################
#######  J A C K P O T  -  SOLO BLOCK!  #########
{"event":"SOLO_BLOCK_FOUND","payoutSat":312512345,"BTC":3.12512345}
################################################
```
The alarm latches (LED + buzzer + relay) until you reset the board.

## Notes

- Works for **true solo** setups where the coinbase pays your address directly
  (solo.ckpool, Public Pool solo, your own template). If a pool pays you out
  separately, the coinbase won't carry your script — use the pool's win notice.
- The coinbase is parsed from the first ~700 bytes of the block (bounds-checked);
  that comfortably covers the coinbase transaction.
- Written to the ZMTP spec; validate against your real node (serial logs each
  handshake step). It cannot change your odds — it just never lets you miss the
  moment you win.
