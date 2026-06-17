# ESP32 ZMQ Fleet — live Bitcoin node visualisation across many ESP32 boards

Firmware for **ESP32-WROOM-32 (NodeMCU, USB-C / CH340)** boards. Each board
subscribes **directly** to your Bitcoin Core node's **ZMQ** feed and runs one
**role** — so a fleet (e.g. your 25 boards) becomes a live, distributed wall of
block/mempool activity driven by your own node.

> **Honest scope.** This is a monitoring / visualisation / alert fleet. It does
> **not** mine and does **not** affect finding blocks. ESP32s cannot meaningfully
> mine Bitcoin. What this *does* is turn your node's real-time ZMQ events into
> something you can see and react to instantly across many boards — and it makes
> direct use of the ZMQ feed you enabled.
>
> It speaks ZMTP 3 (NULL mechanism, SUB socket) over raw TCP. It was written to
> spec but **must be tested against your real bitcoind** — watch the serial
> monitor; it logs every handshake step so you can see exactly where it is.

## 1. Enable ZMQ on the node (once)

In `bitcoin.conf` — publish **all topics on one port** so every board can use the
same port regardless of role:

```
zmqpubhashblock=tcp://0.0.0.0:28332
zmqpubhashtx=tcp://0.0.0.0:28332
zmqpubrawblock=tcp://0.0.0.0:28332
zmqpubrawtx=tcp://0.0.0.0:28332
zmqpubsequence=tcp://0.0.0.0:28332
```

Restart the node and verify:

```
bitcoin-cli getzmqnotifications
```

You should see all five topics on `:28332`. (`0.0.0.0` lets the boards reach it
over the LAN. Make sure the node host's firewall allows TCP 28332.)

## 2. Flash the boards (Arduino IDE)

1. Install the **ESP32 board package** (Boards Manager → "esp32" by Espressif).
2. Board: **ESP32 Dev Module**. Port: the CH340 serial port.
3. Open `esp32_zmq_node.ino` and set, near the top:
   - `WIFI_SSID`, `WIFI_PASS`
   - `ZMQ_HOST` = your node's LAN IP, `ZMQ_PORT = 28332`
   - `NODE_ROLE` = the role for *this* board (see below)
4. Upload. Open Serial Monitor @ **115200** to watch it connect and stream JSON.

Give each board a **different `NODE_ROLE`** before flashing it — that is what
makes every board a unique ZMQ task.

## 3. Roles (each board picks one)

| Role | ZMQ topic | What the board does |
|------|-----------|---------------------|
| `ROLE_BLOCK_BEACON` | hashblock | Hard LED burst on **every new block** — the network heartbeat |
| `ROLE_BLOCK_HASH_TICKER` | hashblock | Prints the full **block hash** each block (a block-clock log) |
| `ROLE_BLOCK_TIMER` | hashblock | Marks each new block; track time-between-blocks |
| `ROLE_BLOCK_WATCHDOG` | hashblock | **SOS** LED if no block for `STALL_MINUTES` (network stall alarm) |
| `ROLE_TX_FIREFLY` | hashtx | Tiny blink **per transaction** — live mempool "fireflies" |
| `ROLE_MEMPOOL_PULSE` | hashtx | LED **brightness ∝ tx throughput** (how busy the mempool is) |
| `ROLE_RAWBLOCK_SIZE` | rawblock | Reports each block's **size** (bytes/MB); more blinks = bigger block |
| `ROLE_RAWTX_WHALE` | rawtx | Flags unusually **large transactions** (> `WHALE_TX_BYTES`) |
| `ROLE_SEQUENCE_MONITOR` | sequence | Prints mempool **add/remove** and block **(un)confirm** events |

ZMQ exposes ~5 real data streams, so the roles cluster around them — but each
board has a genuinely distinct behaviour. With 25 boards you can, for example,
dedicate several to a synchronised **block-beacon wall**, a few to **mempool
fireflies**, one **watchdog**, one **hash ticker**, etc.

## 4. Output

These are bare boards: output is the **on-board LED (GPIO2)** + **JSON on serial**.
Example serial lines:

```
{"role":"block_beacon","blocksSeen":3,"intervalSec":612.0,"seq":4187}
{"role":"rawblock_size","blocksSeen":3,"bytes":1483911,"MB":1.484}
{"role":"sequence","label":"A","seq":92011}
```

For a bigger visual wall you can wire external LEDs/relays to free GPIOs and
drive them from the same event handler.

## 5. Honest conclusion

- ZMQ gives **instant** events from *your* node — these boards react with almost
  no latency, which is the whole point of the ZMQ feed.
- This will not find blocks or earn BTC. It is a professional, unique way to
  *see* your node and farm live, and a great use for 25 otherwise-idle ESP32s.
- If a board stalls during connect, the serial log shows the last successful
  step (`greeting OK`, `handshake complete`, `subscribed to ...`) — that tells
  us exactly what to fix.
