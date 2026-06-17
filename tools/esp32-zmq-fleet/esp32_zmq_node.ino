// ============================================================================
// ESP32 ZMQ Fleet Node  -  firmware for ESP32-WROOM-32 (NodeMCU, USB-C/CH340)
// ----------------------------------------------------------------------------
// Subscribes DIRECTLY to a Bitcoin Core node's ZMQ feed (ZMTP 3 / NULL / SUB)
// and runs ONE "role" per board, so a fleet of boards each performs a distinct,
// live ZMQ-driven task. Output is the on-board LED (GPIO2) + rich JSON on serial
// (these bare boards have no display).
//
// HONEST NOTE: this is a monitoring / visualisation / alert fleet. It does NOT
// mine and does NOT change anything about finding blocks. It just turns your
// node's real-time ZMQ events into something you can see across 25 boards.
//
// Per board you change exactly ONE line: NODE_ROLE (and WiFi/ZMQ once).
// ============================================================================

#include <WiFi.h>

// ----------------------------- USER CONFIG ----------------------------------
static const char * WIFI_SSID = "YOUR_WIFI";
static const char * WIFI_PASS = "YOUR_WIFI_PASSWORD";

// IP of the machine running bitcoind (the node), and the ZMQ port you enabled
// in bitcoin.conf (e.g. zmqpubhashblock=tcp://0.0.0.0:28332).
static const char * ZMQ_HOST = "192.168.178.74";
static const uint16_t ZMQ_PORT = 28332;   // hashblock/hashtx port; rawblock uses 28333 etc.

// Set THIS per board to give it its unique job (see roles below):
#define NODE_ROLE ROLE_BLOCK_BEACON

// ------------------------------- ROLES --------------------------------------
#define ROLE_BLOCK_BEACON      1  // flash hard on every new block (topic: hashblock)
#define ROLE_BLOCK_TIMER       2  // heartbeat that speeds up the longer since last block (hashblock)
#define ROLE_BLOCK_WATCHDOG    3  // SOS if no block for > STALL_MINUTES (hashblock)
#define ROLE_BLOCK_HASH_TICKER 4  // print full block hash each block (hashblock)
#define ROLE_TX_FIREFLY        5  // tiny blink per transaction (topic: hashtx)
#define ROLE_MEMPOOL_PULSE     6  // LED brightness ~ tx throughput (hashtx)
#define ROLE_RAWBLOCK_SIZE     7  // report block size in bytes/MB (topic: rawblock, port+1)
#define ROLE_RAWTX_WHALE       8  // flag unusually large txs (topic: rawtx)
#define ROLE_SEQUENCE_MONITOR  9  // print mempool add/remove + block (un)confirm (topic: sequence)

#define STALL_MINUTES 20          // for ROLE_BLOCK_WATCHDOG
#define WHALE_TX_BYTES 50000      // for ROLE_RAWTX_WHALE

#define LED_PIN 2                 // on-board blue LED on WROOM-32 dev boards

// --------------------------- ROLE -> ZMQ TOPIC ------------------------------
static const char * roleTopic()
{
    switch (NODE_ROLE) {
        case ROLE_TX_FIREFLY:
        case ROLE_MEMPOOL_PULSE:   return "hashtx";
        case ROLE_RAWBLOCK_SIZE:   return "rawblock";
        case ROLE_RAWTX_WHALE:     return "rawtx";
        case ROLE_SEQUENCE_MONITOR:return "sequence";
        default:                   return "hashblock";
    }
}

// ------------------------------- STATE --------------------------------------
WiFiClient client;
static uint32_t blocksSeen = 0;
static uint32_t txCount = 0;
static int64_t  lastBlockMs = 0;
static int64_t  lastTxWindowMs = 0;
static uint32_t txInWindow = 0;
static float    txPerMin = 0;

// ----------------------- low-level socket helpers ---------------------------
static bool readExact(uint8_t * buf, size_t n, uint32_t timeout_ms)
{
    size_t got = 0;
    uint32_t start = millis();
    while (got < n) {
        if (!client.connected()) return false;
        int avail = client.available();
        if (avail > 0) {
            int r = client.read(buf + got, n - got);
            if (r > 0) { got += r; start = millis(); }
        } else {
            if (millis() - start > timeout_ms) return false;
            delay(1);
        }
    }
    return true;
}

// Read one ZMTP frame. Stores up to storeMax bytes of the body into store, and
// always reports the true body size (extra bytes are read and discarded, so big
// rawblock frames don't need to fit in RAM). Returns false on error.
static bool readFrame(uint8_t * store, size_t storeMax, uint64_t * outSize, bool * more, bool * isCommand)
{
    uint8_t flags;
    if (!readExact(&flags, 1, 15000)) return false;
    *more = flags & 0x01;
    bool longSize = flags & 0x02;
    *isCommand = flags & 0x04;

    uint64_t size = 0;
    if (longSize) {
        uint8_t s8[8];
        if (!readExact(s8, 8, 5000)) return false;
        for (int i = 0; i < 8; i++) size = (size << 8) | s8[i];
    } else {
        uint8_t s1;
        if (!readExact(&s1, 1, 5000)) return false;
        size = s1;
    }
    *outSize = size;

    uint64_t remaining = size;
    size_t stored = 0;
    uint8_t scratch[256];
    while (remaining > 0) {
        size_t chunk = remaining > sizeof(scratch) ? sizeof(scratch) : (size_t) remaining;
        if (!readExact(scratch, chunk, 10000)) return false;
        for (size_t i = 0; i < chunk && stored < storeMax; i++) store[stored++] = scratch[i];
        remaining -= chunk;
    }
    return true;
}

// ----------------------------- ZMTP handshake -------------------------------
static bool zmtpConnect()
{
    Serial.printf("[zmtp] connecting to %s:%u ...\n", ZMQ_HOST, ZMQ_PORT);
    if (!client.connect(ZMQ_HOST, ZMQ_PORT, 8000)) {
        Serial.println("[zmtp] TCP connect failed");
        return false;
    }

    // 1) Greeting (64 bytes): signature + version 3.1 + NULL mechanism + as-server=0
    uint8_t greeting[64] = {0};
    greeting[0] = 0xFF;
    greeting[9] = 0x7F;
    greeting[10] = 0x03;          // version major
    greeting[11] = 0x01;          // version minor
    memcpy(greeting + 12, "NULL", 4);
    greeting[32] = 0x00;          // as-server = false (we are the client)
    client.write(greeting, 64);

    uint8_t peer[64];
    if (!readExact(peer, 64, 8000)) { Serial.println("[zmtp] no greeting"); client.stop(); return false; }
    if (peer[0] != 0xFF) { Serial.println("[zmtp] bad greeting signature"); client.stop(); return false; }
    Serial.println("[zmtp] greeting OK");

    // 2) READY command, advertising Socket-Type = SUB
    uint8_t body[64]; int n = 0;
    body[n++] = 0x05; memcpy(body + n, "READY", 5); n += 5;
    body[n++] = 0x0B; memcpy(body + n, "Socket-Type", 11); n += 11;
    body[n++] = 0x00; body[n++] = 0x00; body[n++] = 0x00; body[n++] = 0x03; // value length = 3
    memcpy(body + n, "SUB", 3); n += 3;
    uint8_t hdr[2] = { 0x04, (uint8_t) n };   // flags=command(short), size
    client.write(hdr, 2);
    client.write(body, n);

    // 3) Read peer READY (consume one command frame)
    uint8_t tmp[64]; uint64_t sz; bool more, cmd;
    if (!readFrame(tmp, sizeof(tmp), &sz, &more, &cmd)) { Serial.println("[zmtp] no peer READY"); client.stop(); return false; }
    Serial.println("[zmtp] handshake complete");

    // 4) SUBSCRIBE: message frame, body = 0x01 + topic (empty topic = all)
    const char * topic = roleTopic();
    uint8_t sub[64]; int sn = 0;
    sub[sn++] = 0x01;                                  // 0x01 = subscribe
    size_t tl = strlen(topic);
    memcpy(sub + sn, topic, tl); sn += tl;
    uint8_t shdr[2] = { 0x00, (uint8_t) sn };          // flags=short msg, final
    client.write(shdr, 2);
    client.write(sub, sn);
    Serial.printf("[zmtp] subscribed to '%s'\n", topic);
    return true;
}

// --------------------------- LED helpers ------------------------------------
static void ledBurst(int times, int on_ms, int off_ms)
{
    for (int i = 0; i < times; i++) { digitalWrite(LED_PIN, HIGH); delay(on_ms); digitalWrite(LED_PIN, LOW); delay(off_ms); }
}

// ------------------------- ROLE event handler -------------------------------
// Called for every complete ZMQ notification. topic/topicLen identify the
// stream; body/bodySize is the payload (hash = 32 bytes, raw = full size); seq
// is the publisher sequence number.
static void onEvent(const char * topic, size_t topicLen, const uint8_t * body, uint64_t bodySize, uint32_t seq)
{
    int64_t now = millis();

    if (NODE_ROLE == ROLE_BLOCK_BEACON && topicLen == 9) {
        blocksSeen++;
        float interval = lastBlockMs ? (now - lastBlockMs) / 1000.0f : 0;
        lastBlockMs = now;
        Serial.printf("{\"role\":\"block_beacon\",\"blocksSeen\":%u,\"intervalSec\":%.1f,\"seq\":%u}\n", blocksSeen, interval, seq);
        ledBurst(10, 60, 60);                       // hard celebratory burst
    }
    else if (NODE_ROLE == ROLE_BLOCK_HASH_TICKER && topicLen == 9) {
        blocksSeen++;
        char hex[65]; for (int i = 0; i < 32 && i < (int)bodySize; i++) sprintf(hex + i*2, "%02x", body[31 - i]); // display big-endian
        lastBlockMs = now;
        Serial.printf("{\"role\":\"hash_ticker\",\"blocksSeen\":%u,\"blockHash\":\"%s\"}\n", blocksSeen, hex);
        ledBurst(3, 120, 120);
    }
    else if (NODE_ROLE == ROLE_BLOCK_TIMER && topicLen == 9) {
        blocksSeen++; lastBlockMs = now;
        Serial.printf("{\"role\":\"block_timer\",\"blocksSeen\":%u,\"event\":\"new_block\"}\n", blocksSeen);
        ledBurst(2, 200, 100);
    }
    else if (NODE_ROLE == ROLE_BLOCK_WATCHDOG && topicLen == 9) {
        lastBlockMs = now;
        Serial.println("{\"role\":\"watchdog\",\"event\":\"block_seen\",\"status\":\"ok\"}");
    }
    else if (NODE_ROLE == ROLE_TX_FIREFLY && topicLen == 6) {
        txCount++; txInWindow++;
        ledBurst(1, 4, 0);                          // tiny firefly blink per tx
        if (txCount % 50 == 0) Serial.printf("{\"role\":\"tx_firefly\",\"txCount\":%u}\n", txCount);
    }
    else if (NODE_ROLE == ROLE_MEMPOOL_PULSE && topicLen == 6) {
        txCount++; txInWindow++;                    // brightness handled in loop() from txPerMin
    }
    else if (NODE_ROLE == ROLE_RAWBLOCK_SIZE && topicLen == 8) {
        blocksSeen++; lastBlockMs = now;
        Serial.printf("{\"role\":\"rawblock_size\",\"blocksSeen\":%u,\"bytes\":%llu,\"MB\":%.3f}\n",
                      blocksSeen, (unsigned long long) bodySize, bodySize / 1000000.0);
        ledBurst((int)(bodySize / 250000) + 1, 80, 80);  // more blinks = bigger block
    }
    else if (NODE_ROLE == ROLE_RAWTX_WHALE && topicLen == 5) {
        txCount++;
        if (bodySize >= WHALE_TX_BYTES) {
            Serial.printf("{\"role\":\"whale\",\"event\":\"large_tx\",\"bytes\":%llu}\n", (unsigned long long) bodySize);
            ledBurst(6, 100, 80);
        }
    }
    else if (NODE_ROLE == ROLE_SEQUENCE_MONITOR && topicLen == 8) {
        // sequence body: 32-byte hash + 1 char label (A=add, R=remove, C=connect, D=disconnect)
        char label = bodySize >= 33 ? (char) body[32] : '?';
        Serial.printf("{\"role\":\"sequence\",\"label\":\"%c\",\"seq\":%u}\n", label, seq);
        ledBurst(1, 30, 0);
    }
}

// --------------------------- read one notification --------------------------
// A bitcoind ZMQ notification is multipart: [topic][body][sequence(4B LE)].
static void pumpZmq()
{
    uint8_t topic[16]; uint64_t topicSize; bool more, cmd;
    if (!readFrame(topic, sizeof(topic), &topicSize, &more, &cmd)) { client.stop(); return; }
    if (cmd) return;                                // ignore stray commands (e.g. PING)
    if (!more) return;                              // malformed; expect more parts

    static uint8_t body[64];                        // hashes fit; raw payloads are size-counted only
    uint64_t bodySize; bool bmore;
    if (!readFrame(body, sizeof(body), &bodySize, &bmore, &cmd)) { client.stop(); return; }

    uint32_t seq = 0;
    if (bmore) {
        uint8_t s[8]; uint64_t ssz; bool smore;
        if (!readFrame(s, sizeof(s), &ssz, &smore, &cmd)) { client.stop(); return; }
        if (ssz >= 4) seq = (uint32_t)s[0] | ((uint32_t)s[1] << 8) | ((uint32_t)s[2] << 16) | ((uint32_t)s[3] << 24);
    }

    onEvent((const char *) topic, (size_t) topicSize, body, bodySize, seq);
}

// -------------------------------- Arduino -----------------------------------
void setup()
{
    Serial.begin(115200);
    pinMode(LED_PIN, OUTPUT);
    digitalWrite(LED_PIN, LOW);
    delay(500);
    Serial.printf("\nESP32 ZMQ Fleet Node | role=%d | topic=%s\n", NODE_ROLE, roleTopic());

    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASS);
    Serial.print("[wifi] connecting");
    while (WiFi.status() != WL_CONNECTED) { delay(300); Serial.print("."); digitalWrite(LED_PIN, !digitalRead(LED_PIN)); }
    digitalWrite(LED_PIN, LOW);
    Serial.printf("\n[wifi] connected, ip=%s\n", WiFi.localIP().toString().c_str());

    lastTxWindowMs = millis();
}

void loop()
{
    if (WiFi.status() != WL_CONNECTED) { WiFi.reconnect(); delay(1000); return; }

    if (!client.connected()) {
        if (!zmtpConnect()) { ledBurst(1, 40, 460); return; }   // slow blink while down, retry
    }

    if (client.available()) {
        pumpZmq();
    }

    int64_t now = millis();

    // rolling tx-rate (1-minute window) for firefly/pulse roles
    if (now - lastTxWindowMs >= 60000) {
        txPerMin = txInWindow; txInWindow = 0; lastTxWindowMs = now;
        if (NODE_ROLE == ROLE_MEMPOOL_PULSE) Serial.printf("{\"role\":\"mempool_pulse\",\"txPerMin\":%.0f}\n", txPerMin);
    }
    if (NODE_ROLE == ROLE_MEMPOOL_PULSE) {
        int duty = (int) (txPerMin > 600 ? 255 : txPerMin * 255 / 600);  // brightness ~ busyness
        analogWrite(LED_PIN, duty);
    }

    // watchdog: SOS if the network has been quiet too long
    if (NODE_ROLE == ROLE_BLOCK_WATCHDOG && lastBlockMs > 0 && (now - lastBlockMs) > (int64_t) STALL_MINUTES * 60000) {
        Serial.printf("{\"role\":\"watchdog\",\"event\":\"STALL\",\"minutes\":%lld}\n", (now - lastBlockMs) / 60000);
        ledBurst(3, 150, 150); ledBurst(3, 400, 150); ledBurst(3, 150, 400); // SOS
        lastBlockMs = now;  // avoid spamming; re-arm
    }

    delay(2);
}
