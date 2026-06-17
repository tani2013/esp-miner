// ============================================================================
// ESP32 Solo Jackpot Alarm  -  fires the instant YOUR solo block is found
// ----------------------------------------------------------------------------
// Subscribes to your Bitcoin Core node's ZMQ `rawblock` feed, parses the
// coinbase transaction of every new block, and checks whether any coinbase
// output pays YOUR address (its scriptPubKey). If it does, that block was mined
// by you (true solo) -> JACKPOT alarm: LED + optional buzzer/relay + serial +
// a broadcast on the swarm bus so a whole fleet celebrates.
//
// HONEST SCOPE: this does NOT help find a block (nothing can shortcut PoW). It
// is the unique, real "connection to BTC" for a solo miner: the one moment that
// matters, announced physically the instant it happens.
//
// On every block it also prints the winning coinbase script + value, so you can
// see the parser working live (and who got paid) long before you ever win.
// ============================================================================

#include <WiFi.h>
#include <WiFiUdp.h>

// ----------------------------- USER CONFIG ----------------------------------
static const char * WIFI_SSID = "YOUR_WIFI";
static const char * WIFI_PASS = "YOUR_WIFI_PASSWORD";
static const char * ZMQ_HOST  = "192.168.178.74";
static const uint16_t ZMQ_PORT = 28332;     // must publish zmqpubrawblock on this port
static const uint16_t SWARM_PORT = 28900;   // broadcast the win to the fleet

// Your payout scriptPubKey in hex. Get it once on the node with:
//   bitcoin-cli getaddressinfo <your_btc_address> | grep scriptPubKey
// e.g. a bech32 address gives "0014<20-byte-hash>".
static const char * MY_SCRIPTPUBKEY_HEX = "0014ffffffffffffffffffffffffffffffffffffffff";

#define LED_PIN    2     // on-board LED
#define BUZZER_PIN 4     // optional active buzzer (GPIO4) - safe to leave unconnected
#define RELAY_PIN  5     // optional relay/siren/light (GPIO5) - safe to leave unconnected

// ------------------------------- STATE --------------------------------------
WiFiClient client;
WiFiUDP udp;
static uint8_t  myScript[64];
static size_t   myScriptLen = 0;
static uint32_t blocksChecked = 0;
static bool     jackpot = false;

// ----------------------------- hex helper -----------------------------------
static size_t hexToBytes(const char * hex, uint8_t * out, size_t outMax)
{
    size_t n = 0;
    for (size_t i = 0; hex[i] && hex[i+1] && n < outMax; i += 2) {
        auto nib = [](char c) -> int {
            if (c >= '0' && c <= '9') return c - '0';
            if (c >= 'a' && c <= 'f') return c - 'a' + 10;
            if (c >= 'A' && c <= 'F') return c - 'A' + 10;
            return -1;
        };
        int hi = nib(hex[i]), lo = nib(hex[i+1]);
        if (hi < 0 || lo < 0) break;
        out[n++] = (uint8_t)((hi << 4) | lo);
    }
    return n;
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

// Reads a frame, storing up to storeMax bytes of the body (rest discarded).
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
        if (!readExact(scratch, chunk, 12000)) return false;
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
    const char * topic = "rawblock";
    uint8_t sub[32]; int sn=0; sub[sn++]=0x01; memcpy(sub+sn,topic,strlen(topic)); sn+=strlen(topic);
    uint8_t sh[2]={0x00,(uint8_t)sn}; client.write(sh,2); client.write(sub,sn);
    Serial.println("[zmtp] subscribed 'rawblock'");
    return true;
}

// ------------------------- coinbase parsing ---------------------------------
static uint64_t readVarint(const uint8_t * b, size_t len, size_t * c)
{
    if (*c >= len) return UINT64_MAX;
    uint8_t p = b[(*c)++];
    if (p < 0xfd) return p;
    if (p == 0xfd) { if (*c + 2 > len) return UINT64_MAX; uint64_t v = b[*c] | (b[*c+1] << 8); *c += 2; return v; }
    if (p == 0xfe) { if (*c + 4 > len) return UINT64_MAX; uint64_t v = (uint64_t)b[*c] | ((uint64_t)b[*c+1]<<8) | ((uint64_t)b[*c+2]<<16) | ((uint64_t)b[*c+3]<<24); *c += 4; return v; }
    if (*c + 8 > len) return UINT64_MAX; uint64_t v = 0; for (int i = 0; i < 8; i++) v |= (uint64_t)b[*c+i] << (8*i); *c += 8; return v;
}

// Parse the coinbase tx from the start of a block buffer and check whether any
// output's scriptPubKey equals our address. Returns true on match; *payout gets
// the matched output value (sats). Bounds-checked against len.
static bool coinbasePaysMe(const uint8_t * b, size_t len, uint64_t * payout)
{
    size_t c = 80;                          // skip 80-byte block header
    uint64_t txCount = readVarint(b, len, &c);
    if (txCount == UINT64_MAX) return false;
    c += 4;                                 // coinbase version
    uint64_t inCount = readVarint(b, len, &c);
    if (inCount == UINT64_MAX) return false;
    for (uint64_t i = 0; i < inCount; i++) {
        c += 36;                            // prevout (32 hash + 4 index, all zero for coinbase)
        uint64_t sl = readVarint(b, len, &c);
        if (sl == UINT64_MAX) return false;
        c += (size_t) sl;                   // scriptSig
        c += 4;                             // sequence
        if (c > len) return false;
    }
    uint64_t outCount = readVarint(b, len, &c);
    if (outCount == UINT64_MAX) return false;
    for (uint64_t i = 0; i < outCount; i++) {
        if (c + 8 > len) return false;
        uint64_t value = 0; for (int k = 0; k < 8; k++) value |= (uint64_t)b[c+k] << (8*k);
        c += 8;
        uint64_t sl = readVarint(b, len, &c);
        if (sl == UINT64_MAX || c + sl > len) return false;
        if (sl == myScriptLen && memcmp(b + c, myScript, myScriptLen) == 0) {
            *payout = value;
            return true;
        }
        // log the first output so you can see the parser working each block
        if (i == 0) {
            char hex[80] = {0}; size_t show = sl < 36 ? sl : 36;
            for (size_t k = 0; k < show; k++) sprintf(hex + k*2, "%02x", b[c+k]);
            Serial.printf("{\"block\":%u,\"coinbaseOut0\":\"%s\",\"valueSat\":%llu}\n",
                          blocksChecked, hex, (unsigned long long) value);
        }
        c += (size_t) sl;
    }
    return false;
}

// ------------------------------- alarm --------------------------------------
static void fireJackpot(uint64_t payoutSat)
{
    jackpot = true;
    char pkt[160];
    snprintf(pkt, sizeof(pkt), "{\"event\":\"SOLO_BLOCK_FOUND\",\"payoutSat\":%llu,\"BTC\":%.8f}",
             (unsigned long long) payoutSat, payoutSat / 1e8);
    udp.beginPacket(IPAddress(255,255,255,255), SWARM_PORT);
    udp.write((const uint8_t *) pkt, strlen(pkt));
    udp.endPacket();
    Serial.println("################################################");
    Serial.println("#######  J A C K P O T  -  SOLO BLOCK!  #########");
    Serial.println(pkt);
    Serial.println("################################################");
}

// ------------------------- read one notification ----------------------------
static void pumpZmq()
{
    uint8_t topic[16]; uint64_t tSize; bool more, cmd;
    if (!readFrame(topic, sizeof(topic), &tSize, &more, &cmd)) { client.stop(); return; }
    if (cmd || !more) return;

    static uint8_t blockHead[700];          // enough for header + coinbase tx
    uint64_t bSize; bool bmore;
    if (!readFrame(blockHead, sizeof(blockHead), &bSize, &bmore, &cmd)) { client.stop(); return; }
    // (sequence frame, if present, is read & discarded)
    if (bmore) { uint8_t s[8]; uint64_t ssz; bool sm; readFrame(s, sizeof(s), &ssz, &sm, &cmd); }

    if (tSize == 8) {                        // "rawblock"
        blocksChecked++;
        size_t have = bSize < sizeof(blockHead) ? (size_t) bSize : sizeof(blockHead);
        uint64_t payout = 0;
        if (coinbasePaysMe(blockHead, have, &payout)) fireJackpot(payout);
    }
}

// -------------------------------- Arduino -----------------------------------
void setup()
{
    Serial.begin(115200);
    pinMode(LED_PIN, OUTPUT); pinMode(BUZZER_PIN, OUTPUT); pinMode(RELAY_PIN, OUTPUT);
    digitalWrite(LED_PIN, LOW); digitalWrite(BUZZER_PIN, LOW); digitalWrite(RELAY_PIN, LOW);
    delay(400);

    myScriptLen = hexToBytes(MY_SCRIPTPUBKEY_HEX, myScript, sizeof(myScript));
    Serial.printf("\nSolo Jackpot Alarm | watching scriptPubKey of %u bytes\n", (unsigned) myScriptLen);

    WiFi.mode(WIFI_STA); WiFi.begin(WIFI_SSID, WIFI_PASS);
    Serial.print("[wifi] connecting");
    while (WiFi.status() != WL_CONNECTED) { delay(300); Serial.print("."); }
    Serial.printf("\n[wifi] ip=%s\n", WiFi.localIP().toString().c_str());
    udp.begin(SWARM_PORT);
}

void loop()
{
    // Latched alarm: once you win, scream forever until reset.
    if (jackpot) {
        digitalWrite(RELAY_PIN, HIGH);
        for (int i = 0; i < 6; i++) { digitalWrite(LED_PIN, HIGH); digitalWrite(BUZZER_PIN, HIGH); delay(120);
                                      digitalWrite(LED_PIN, LOW);  digitalWrite(BUZZER_PIN, LOW);  delay(80); }
        delay(400);
        return;
    }

    if (WiFi.status() != WL_CONNECTED) { WiFi.reconnect(); delay(1000); return; }
    if (!client.connected()) { if (!zmtpConnect()) { digitalWrite(LED_PIN,HIGH); delay(60); digitalWrite(LED_PIN,LOW); delay(1500); return; } }
    if (client.available()) pumpZmq();
    delay(2);
}
