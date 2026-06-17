// ============================================================================
// ESP32 Anomaly Swarm  -  25-agent collaborative Bitcoin network watcher
// ----------------------------------------------------------------------------
// Each ESP32-WROOM-32 board is one AGENT. It subscribes to your Bitcoin Core
// node's ZMQ feed (ZMTP 3 / NULL / SUB) and hunts for ONE real network anomaly.
// Agents collaborate over a UDP "swarm bus": every finding is broadcast on the
// LAN so all agents - and a COORDINATOR agent - see the collective picture and
// can escalate correlated events.
//
// HONEST SCOPE: this detects REAL anomalies (chain reorgs, stalls, fee/spam
// spikes, empty/full blocks, dropped messages). It does NOT and CANNOT find a
// block or shortcut proof-of-work - SHA-256 has no exploitable pattern. This is
// genuine network intelligence/security, not magic.
//
// Per board set: AGENT_ID (1..25) and NODE_ROLE. WiFi/ZMQ once.
// ============================================================================

#include <WiFi.h>
#include <WiFiUdp.h>

// ----------------------------- USER CONFIG ----------------------------------
static const char * WIFI_SSID = "YOUR_WIFI";
static const char * WIFI_PASS = "YOUR_WIFI_PASSWORD";
static const char * ZMQ_HOST  = "192.168.178.74";   // your node's LAN IP
static const uint16_t ZMQ_PORT = 28332;             // all topics on one port (see README)
static const uint16_t SWARM_PORT = 28900;           // UDP swarm bus port (any free port)

#define AGENT_ID   1                  // 1..25, unique per board
#define NODE_ROLE  ROLE_REORG_WATCH   // this board's anomaly hunt (see below)

// ------------------------------- ROLES --------------------------------------
#define ROLE_REORG_WATCH     1  // sequence: block 'D'isconnect = chain reorg (critical)
#define ROLE_FAST_BLOCK      2  // hashblock: interval < FAST_SEC
#define ROLE_STALL_WATCH     3  // hashblock: no block for > STALL_SEC
#define ROLE_MEMPOOL_FLOOD   4  // hashtx: tx/min > FLOOD_TPM (spam/flood)
#define ROLE_MEMPOOL_DROUGHT 5  // hashtx: tx/min < DROUGHT_TPM (node maybe isolated)
#define ROLE_WHALE_TX        6  // rawtx: tx bytes > WHALE_BYTES
#define ROLE_DUST_STORM      7  // rawtx: burst of tiny txs (< DUST_BYTES)
#define ROLE_BIG_BLOCK       8  // rawblock: size > BIG_BYTES (near-full block)
#define ROLE_EMPTY_BLOCK     9  // rawblock: size < EMPTY_BYTES (empty/near-empty block)
#define ROLE_SEQ_GAP        10  // any: publisher sequence gap = dropped notifications
#define ROLE_COORDINATOR    11  // no ZMQ; aggregates the swarm bus and escalates

// ----------------------------- THRESHOLDS -----------------------------------
#define FAST_SEC      120
#define STALL_SEC     1800
#define FLOOD_TPM     900
#define DROUGHT_TPM   3
#define WHALE_BYTES   50000
#define DUST_BYTES    250
#define DUST_BURST    40        // tiny txs within a minute to call it a storm
#define BIG_BYTES     3900000
#define EMPTY_BYTES   1200
#define CORR_WINDOW_MS 15000    // coordinator: critical alerts within this window = correlated

#define LED_PIN 2

// ------------------------------- STATE --------------------------------------
WiFiClient client;
WiFiUDP udp;
static int64_t  lastBlockMs = 0;
static uint32_t blocksSeen = 0;
static uint32_t txInWindow = 0, dustInWindow = 0;
static int64_t  windowStartMs = 0;
static uint32_t lastSeq = 0; static bool haveSeq = false;
// coordinator state
static uint32_t critCount = 0; static int64_t lastCritMs = 0;
static uint32_t totalAlerts = 0;

static const char * roleName()
{
    switch (NODE_ROLE) {
        case ROLE_REORG_WATCH: return "reorg_watch"; case ROLE_FAST_BLOCK: return "fast_block";
        case ROLE_STALL_WATCH: return "stall_watch"; case ROLE_MEMPOOL_FLOOD: return "mempool_flood";
        case ROLE_MEMPOOL_DROUGHT: return "mempool_drought"; case ROLE_WHALE_TX: return "whale_tx";
        case ROLE_DUST_STORM: return "dust_storm"; case ROLE_BIG_BLOCK: return "big_block";
        case ROLE_EMPTY_BLOCK: return "empty_block"; case ROLE_SEQ_GAP: return "seq_gap";
        case ROLE_COORDINATOR: return "coordinator"; default: return "unknown";
    }
}

static const char * roleTopic()
{
    switch (NODE_ROLE) {
        case ROLE_MEMPOOL_FLOOD: case ROLE_MEMPOOL_DROUGHT: return "hashtx";
        case ROLE_WHALE_TX: case ROLE_DUST_STORM: return "rawtx";
        case ROLE_BIG_BLOCK: case ROLE_EMPTY_BLOCK: return "rawblock";
        case ROLE_REORG_WATCH: return "sequence";
        case ROLE_SEQ_GAP: return "hashtx";   // any high-rate topic is fine for gap detection
        default: return "hashblock";          // fast/stall block roles
    }
}

// ----------------------- low-level socket helpers ---------------------------
static bool readExact(uint8_t * buf, size_t n, uint32_t timeout_ms)
{
    size_t got = 0; uint32_t start = millis();
    while (got < n) {
        if (!client.connected()) return false;
        int r = client.read(buf + got, n - got);
        if (r > 0) { got += r; start = millis(); }
        else { if (millis() - start > timeout_ms) return false; delay(1); }
    }
    return true;
}

static bool readFrame(uint8_t * store, size_t storeMax, uint64_t * outSize, bool * more, bool * isCommand)
{
    uint8_t flags;
    if (!readExact(&flags, 1, 20000)) return false;
    *more = flags & 0x01; bool longSize = flags & 0x02; *isCommand = flags & 0x04;
    uint64_t size = 0;
    if (longSize) { uint8_t s8[8]; if (!readExact(s8, 8, 5000)) return false; for (int i = 0; i < 8; i++) size = (size << 8) | s8[i]; }
    else { uint8_t s1; if (!readExact(&s1, 1, 5000)) return false; size = s1; }
    *outSize = size;
    uint64_t remaining = size; size_t stored = 0; uint8_t scratch[256];
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
    Serial.printf("[zmtp] connect %s:%u\n", ZMQ_HOST, ZMQ_PORT);
    if (!client.connect(ZMQ_HOST, ZMQ_PORT, 8000)) { Serial.println("[zmtp] TCP fail"); return false; }
    uint8_t g[64] = {0}; g[0]=0xFF; g[9]=0x7F; g[10]=0x03; g[11]=0x01; memcpy(g+12,"NULL",4); g[32]=0x00;
    client.write(g, 64);
    uint8_t peer[64];
    if (!readExact(peer, 64, 8000) || peer[0] != 0xFF) { Serial.println("[zmtp] greeting fail"); client.stop(); return false; }
    uint8_t body[64]; int n=0; body[n++]=0x05; memcpy(body+n,"READY",5); n+=5;
    body[n++]=0x0B; memcpy(body+n,"Socket-Type",11); n+=11; body[n++]=0;body[n++]=0;body[n++]=0;body[n++]=3; memcpy(body+n,"SUB",3); n+=3;
    uint8_t h[2]={0x04,(uint8_t)n}; client.write(h,2); client.write(body,n);
    uint8_t tmp[64]; uint64_t sz; bool more, cmd;
    if (!readFrame(tmp, sizeof(tmp), &sz, &more, &cmd)) { Serial.println("[zmtp] READY fail"); client.stop(); return false; }
    const char * topic = roleTopic();
    uint8_t sub[64]; int sn=0; sub[sn++]=0x01; size_t tl=strlen(topic); memcpy(sub+sn,topic,tl); sn+=tl;
    uint8_t sh[2]={0x00,(uint8_t)sn}; client.write(sh,2); client.write(sub,sn);
    Serial.printf("[zmtp] subscribed '%s' (agent %d, role %s)\n", topic, AGENT_ID, roleName());
    return true;
}

// --------------------------- LED + swarm bus --------------------------------
static void ledPattern(int sev)
{
    int times = sev >= 3 ? 12 : (sev == 2 ? 4 : 1);
    int d = sev >= 3 ? 70 : 120;
    for (int i = 0; i < times; i++) { digitalWrite(LED_PIN, HIGH); delay(d); digitalWrite(LED_PIN, LOW); delay(d); }
}

// Broadcast a finding on the UDP swarm bus and surface it locally.
static void raise(int sev, const char * detail)
{
    totalAlerts++;
    char pkt[200];
    snprintf(pkt, sizeof(pkt),
        "{\"agent\":%d,\"role\":\"%s\",\"sev\":%d,\"blocksSeen\":%u,\"detail\":\"%s\"}",
        AGENT_ID, roleName(), sev, blocksSeen, detail);
    udp.beginPacket(IPAddress(255,255,255,255), SWARM_PORT);
    udp.write((const uint8_t *) pkt, strlen(pkt));
    udp.endPacket();
    Serial.println(pkt);
    ledPattern(sev);
}

// Coordinator: ingest the swarm bus, escalate correlated criticals.
static void coordinatorIngest()
{
    int sz = udp.parsePacket();
    if (sz <= 0) return;
    char buf[220]; int len = udp.read(buf, sizeof(buf) - 1); if (len <= 0) return; buf[len] = '\0';
    Serial.printf("[swarm] %s\n", buf);
    if (strstr(buf, "\"sev\":3")) {
        int64_t now = millis();
        if (now - lastCritMs <= CORR_WINDOW_MS) critCount++; else critCount = 1;
        lastCritMs = now;
        if (critCount >= 2) {
            Serial.printf("{\"coordinator\":%d,\"event\":\"CORRELATED_ANOMALY\",\"criticals\":%u}\n", AGENT_ID, critCount);
            ledPattern(3); ledPattern(3);   // big combined alarm
        } else {
            ledPattern(3);
        }
    }
}

// ------------------------- per-event detection ------------------------------
static void onEvent(const char * topic, size_t topicLen, const uint8_t * body, uint64_t bodySize, uint32_t seq)
{
    int64_t now = millis();

    // Sequence-gap detector works on any topic that carries a sequence number.
    if (NODE_ROLE == ROLE_SEQ_GAP) {
        if (haveSeq && seq != lastSeq + 1 && seq != 0) {
            char d[64]; snprintf(d, sizeof(d), "seq gap %u->%u", lastSeq, seq);
            raise(2, d);
        }
        lastSeq = seq; haveSeq = true;
        return;
    }

    if (topicLen == 9) { // hashblock
        float interval = lastBlockMs ? (now - lastBlockMs) / 1000.0f : 0;
        lastBlockMs = now; blocksSeen++;
        if (NODE_ROLE == ROLE_FAST_BLOCK && interval > 0 && interval < FAST_SEC) {
            char d[64]; snprintf(d, sizeof(d), "fast block %.0fs", interval); raise(2, d);
        }
    }
    else if (topicLen == 6) { // hashtx
        txInWindow++;
    }
    else if (topicLen == 5) { // rawtx
        txInWindow++;
        if (NODE_ROLE == ROLE_WHALE_TX && bodySize >= WHALE_BYTES) {
            char d[64]; snprintf(d, sizeof(d), "whale tx %llu B", (unsigned long long) bodySize); raise(2, d);
        }
        if (NODE_ROLE == ROLE_DUST_STORM && bodySize <= DUST_BYTES) dustInWindow++;
    }
    else if (topicLen == 8 && (NODE_ROLE == ROLE_BIG_BLOCK || NODE_ROLE == ROLE_EMPTY_BLOCK)) { // rawblock
        blocksSeen++; lastBlockMs = now;
        if (NODE_ROLE == ROLE_BIG_BLOCK && bodySize >= BIG_BYTES) {
            char d[64]; snprintf(d, sizeof(d), "near-full block %.2f MB", bodySize/1e6); raise(2, d);
        }
        if (NODE_ROLE == ROLE_EMPTY_BLOCK && bodySize <= EMPTY_BYTES) {
            char d[64]; snprintf(d, sizeof(d), "empty block %llu B", (unsigned long long) bodySize); raise(2, d);
        }
    }
    else if (topicLen == 8 && NODE_ROLE == ROLE_REORG_WATCH) { // sequence
        char label = bodySize >= 33 ? (char) body[32] : '?';
        if (label == 'D') raise(3, "CHAIN REORG (block disconnected)");
        else if (label == 'C') { blocksSeen++; lastBlockMs = now; }
    }
}

// --------------------------- read one notification --------------------------
static void pumpZmq()
{
    uint8_t topic[16]; uint64_t tSize; bool more, cmd;
    if (!readFrame(topic, sizeof(topic), &tSize, &more, &cmd)) { client.stop(); return; }
    if (cmd || !more) return;
    static uint8_t bodyBuf[64]; uint64_t bSize; bool bmore;
    if (!readFrame(bodyBuf, sizeof(bodyBuf), &bSize, &bmore, &cmd)) { client.stop(); return; }
    uint32_t seq = 0;
    if (bmore) {
        uint8_t s[8]; uint64_t ssz; bool smore;
        if (!readFrame(s, sizeof(s), &ssz, &smore, &cmd)) { client.stop(); return; }
        if (ssz >= 4) seq = (uint32_t)s[0]|((uint32_t)s[1]<<8)|((uint32_t)s[2]<<16)|((uint32_t)s[3]<<24);
    }
    onEvent((const char *) topic, (size_t) tSize, bodyBuf, bSize, seq);
}

// -------------------------------- Arduino -----------------------------------
void setup()
{
    Serial.begin(115200); pinMode(LED_PIN, OUTPUT); digitalWrite(LED_PIN, LOW); delay(400);
    Serial.printf("\nAnomaly Swarm | agent=%d role=%s\n", AGENT_ID, roleName());
    WiFi.mode(WIFI_STA); WiFi.begin(WIFI_SSID, WIFI_PASS);
    Serial.print("[wifi] connecting");
    while (WiFi.status() != WL_CONNECTED) { delay(300); Serial.print("."); }
    Serial.printf("\n[wifi] ip=%s\n", WiFi.localIP().toString().c_str());
    udp.begin(SWARM_PORT);     // listen to the swarm bus (all agents + coordinator)
    windowStartMs = millis();
}

void loop()
{
    if (WiFi.status() != WL_CONNECTED) { WiFi.reconnect(); delay(1000); return; }

    if (NODE_ROLE == ROLE_COORDINATOR) {   // coordinator never touches ZMQ; it listens to the swarm
        coordinatorIngest();
        delay(2);
        return;
    }

    if (!client.connected()) { if (!zmtpConnect()) { ledPattern(1); delay(1500); return; } }
    if (client.available()) pumpZmq();

    // also receive peers' broadcasts so every agent sees the collective picture
    coordinatorIngest();

    int64_t now = millis();
    if (now - windowStartMs >= 60000) {   // 1-minute rolling window
        float tpm = txInWindow;
        if (NODE_ROLE == ROLE_MEMPOOL_FLOOD && tpm > FLOOD_TPM) { char d[48]; snprintf(d,sizeof(d),"flood %.0f tx/min",tpm); raise(2,d); }
        if (NODE_ROLE == ROLE_MEMPOOL_DROUGHT && tpm < DROUGHT_TPM) { char d[48]; snprintf(d,sizeof(d),"drought %.0f tx/min",tpm); raise(3,d); }
        if (NODE_ROLE == ROLE_DUST_STORM && dustInWindow >= DUST_BURST) { char d[48]; snprintf(d,sizeof(d),"dust storm %u/min",dustInWindow); raise(2,d); }
        txInWindow = 0; dustInWindow = 0; windowStartMs = now;
    }

    if (NODE_ROLE == ROLE_STALL_WATCH && lastBlockMs > 0 && (now - lastBlockMs) > (int64_t) STALL_SEC * 1000) {
        raise(3, "network stall - no block");
        lastBlockMs = now;   // re-arm to avoid spamming
    }

    delay(2);
}
