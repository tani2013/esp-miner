// ============================================
// FILE: main/stratum_v2_client.cpp
// Stratum V2 Client - Pure ESP-IDF / LwIP
// ZERO Arduino, ZERO AsyncClient
// ZERO dynamic allocation gjatë gërmimit
// ============================================

#include "stratum_v2_wrapper.h"
#include "stratum_v2_framing.h"

#include "esp_log.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_heap_caps.h"

#include <lwip/sockets.h>
#include <lwip/netdb.h>
#include <string.h>
#include <errno.h>

static const char* TAG = "SV2";

// ============================================
// CONSTANTS
// ============================================
#define SV2_RECV_BUFFER_SIZE  1024
#define SV2_CONNECT_TIMEOUT_S    5
#define SV2_MAX_HOST_LEN        64
#define SV2_PORT_STR_LEN         6

// ============================================
// StratumV2Client Class
// ============================================
class StratumV2Client {
private:
    int             sock;
    bool            connected;
    char            pool_host[SV2_MAX_HOST_LEN];
    uint16_t        pool_port;
    uint8_t         recv_buffer[SV2_RECV_BUFFER_SIZE];
    StratumV2Encoder encoder;
    StratumV2Decoder decoder;
    uint32_t        shares_submitted;

public:
    StratumV2Client()
        : sock(-1),
          connected(false),
          pool_port(0),
          shares_submitted(0)
    {
        memset(pool_host, 0, sizeof(pool_host));
        memset(recv_buffer, 0, sizeof(recv_buffer));
    }

    // ------------------------------------------
    void init(const char* host, uint16_t port) {
        strncpy(pool_host, host, SV2_MAX_HOST_LEN - 1);
        pool_host[SV2_MAX_HOST_LEN - 1] = '\0';
        pool_port = port;
        ESP_LOGI(TAG, "Configured: %s:%d", pool_host, pool_port);
        logHeap("After init");
    }

    // ------------------------------------------
    bool connect_to_pool() {
        if (sock >= 0) {
            lwip_close(sock);
            sock = -1;
            connected = false;
        }

        struct addrinfo hints;
        struct addrinfo* res = nullptr;
        memset(&hints, 0, sizeof(hints));
        hints.ai_family   = AF_INET;
        hints.ai_socktype = SOCK_STREAM;

        char port_str[SV2_PORT_STR_LEN];
        snprintf(port_str, sizeof(port_str), "%d", pool_port);

        ESP_LOGI(TAG, "Resolving %s...", pool_host);
        int err = lwip_getaddrinfo(pool_host, port_str, &hints, &res);
        if (err != 0 || res == nullptr) {
            ESP_LOGE(TAG, "DNS failed: %d", err);
            return false;
        }

        sock = lwip_socket(res->ai_family, res->ai_socktype, res->ai_protocol);
        if (sock < 0) {
            ESP_LOGE(TAG, "Socket failed: errno %d", errno);
            lwip_freeaddrinfo(res);
            return false;
        }

        // Timeouts
        struct timeval timeout;
        timeout.tv_sec  = SV2_CONNECT_TIMEOUT_S;
        timeout.tv_usec = 0;
        lwip_setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
        lwip_setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));

        // TCP_NODELAY - kritike për latency
        int flag = 1;
        lwip_setsockopt(sock, IPPROTO_TCP, TCP_NODELAY, &flag, sizeof(flag));

        ESP_LOGI(TAG, "Connecting to %s:%d...", pool_host, pool_port);
        err = lwip_connect(sock, res->ai_addr, res->ai_addrlen);
        lwip_freeaddrinfo(res);

        if (err != 0) {
            ESP_LOGE(TAG, "Connect failed: errno %d", errno);
            lwip_close(sock);
            sock = -1;
            return false;
        }

        connected = true;
        ESP_LOGI(TAG, "Connected to pool!");
        logHeap("After connect");
        return sendOpeningHandshake();
    }

    // ------------------------------------------
    bool sendRaw(const uint8_t* data, size_t len) {
        if (sock < 0 || !connected) return false;

        size_t sent = 0;
        while (sent < len) {
            int ret = lwip_send(sock, data + sent, len - sent, 0);
            if (ret < 0) {
                ESP_LOGE(TAG, "Send failed: errno %d", errno);
                connected = false;
                lwip_close(sock);
                sock = -1;
                return false;
            }
            sent += ret;
        }
        return true;
    }

    // ------------------------------------------
    int receiveData() {
        if (sock < 0 || !connected) return -1;

        struct timeval timeout;
        timeout.tv_sec  = 0;
        timeout.tv_usec = 100000;  // 100ms
        lwip_setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));

        int len = lwip_recv(sock, recv_buffer, SV2_RECV_BUFFER_SIZE - 1, 0);
        if (len < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) return 0;
            ESP_LOGE(TAG, "Recv error: errno %d", errno);
            connected = false;
            lwip_close(sock);
            sock = -1;
            return -1;
        }
        if (len == 0) {
            ESP_LOGW(TAG, "Pool closed connection");
            connected = false;
            lwip_close(sock);
            sock = -1;
            return -1;
        }
        return len;
    }

    // ------------------------------------------
    bool sendOpeningHandshake() {
        encoder.reset();
        size_t frame_size = encoder.encodeOpeningHandshake(
            "Bitaxe/1.0.0",
            "esp32-miner-001",
            2, 2
        );
        encoder.debugPrintFrame("HANDSHAKE_REQUEST");
        bool ok = sendRaw(encoder.getFrameData(), frame_size);
        if (ok) ESP_LOGI(TAG, "Handshake sent (%d bytes)", (int)frame_size);
        return ok;
    }

    // ------------------------------------------
    bool sendChannelOpenRequest() {
        encoder.reset();
        size_t frame_size = encoder.encodeChannelOpenRequest(
            0x01,
            0x0001,
            1000000000UL,
            8
        );
        encoder.debugPrintFrame("CHANNEL_OPEN_REQUEST");
        return sendRaw(encoder.getFrameData(), frame_size);
    }

    // ------------------------------------------
    void processIncoming(const uint8_t* data, size_t len) {
        StratumV2Frame frame;
        if (!decoder.parseFrame(data, len, frame)) {
            ESP_LOGW(TAG, "Failed to parse frame (len=%d)", (int)len);
            return;
        }

        ESP_LOGD(TAG, "RX: type=0x%02X ch=%d seq=%lu payload=%d bytes",
            frame.message_type,
            frame.channel_id,
            (unsigned long)frame.sequence_number,
            frame.payload_length);

        switch (frame.message_type) {
            case OPENING_HANDSHAKE_RESPONSE:
                ESP_LOGI(TAG, "Handshake accepted!");
                sendChannelOpenRequest();
                break;
            case CHANNEL_OPEN_RESPONSE:
                ESP_LOGI(TAG, "Mining channel opened!");
                break;
            case NEW_TEMPLATE:
                ESP_LOGI(TAG, "New mining template received");
                break;
            case SET_NEW_PREV_HASH:
                handleSetNewPrevHash(frame);
                break;
            case SETDIFF:
                handleSetDiff(frame);
                break;
            default:
                ESP_LOGW(TAG, "Unknown message: 0x%02X", frame.message_type);
                break;
        }
    }

    // ------------------------------------------
    void handleSetNewPrevHash(const StratumV2Frame& frame) {
        if (frame.payload_length < 40) {
            ESP_LOGE(TAG, "SET_NEW_PREV_HASH: payload too short (%d)", frame.payload_length);
            return;
        }
        uint32_t timestamp = StratumV2Decoder::readUint32LE(frame.payload, 32);
        uint32_t nbits     = StratumV2Decoder::readUint32LE(frame.payload, 36);
        ESP_LOGI(TAG, "New prev hash - ts:%lu nbits:0x%08lX",
            (unsigned long)timestamp,
            (unsigned long)nbits);
        // TODO: Kaloo te create_jobs_task përmes work_queue
    }

    // ------------------------------------------
    void handleSetDiff(const StratumV2Frame& frame) {
        if (frame.payload_length < 8) return;
        uint32_t diff_low  = StratumV2Decoder::readUint32LE(frame.payload, 0);
        uint32_t diff_high = StratumV2Decoder::readUint32LE(frame.payload, 4);
        ESP_LOGI(TAG, "New difficulty: %lu (high: %lu)",
            (unsigned long)diff_low,
            (unsigned long)diff_high);
    }

    // ------------------------------------------
    void submitShare(uint32_t nonce, uint32_t ntime, uint32_t version) {
        if (!connected) {
            ESP_LOGW(TAG, "submitShare: not connected, dropping share");
            return;
        }
        encoder.reset();
        uint8_t dummy_coinbase[4] = {0x01, 0x00, 0x00, 0x00};
        size_t frame_size = encoder.encodeSubmitShares(
            0x01,
            shares_submitted,
            sizeof(dummy_coinbase),
            dummy_coinbase,
            nonce, ntime, version
        );
        if (sendRaw(encoder.getFrameData(), frame_size)) {
            shares_submitted++;
            ESP_LOGI(TAG, "Share #%lu submitted (nonce: 0x%08lX)",
                (unsigned long)shares_submitted,
                (unsigned long)nonce);
        }
        if (shares_submitted % 100 == 0) {
            logHeap("After 100 shares");
        }
    }

    // ------------------------------------------
    void loop() {
        if (!connected) {
            ESP_LOGI(TAG, "Reconnecting in 5s...");
            vTaskDelay(pdMS_TO_TICKS(5000));
            connect_to_pool();
            return;
        }
        int len = receiveData();
        if (len > 0) {
            processIncoming(recv_buffer, (size_t)len);
        }
    }

    // ------------------------------------------
    bool isConnected() const {
        return connected && (sock >= 0);
    }

private:
    static void logHeap(const char* label) {
        ESP_LOGI(TAG, "[HEAP] %s - Free: %lu | Min: %lu | Largest: %lu",
            label,
            (unsigned long)esp_get_free_heap_size(),
            (unsigned long)esp_get_minimum_free_heap_size(),
            (unsigned long)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
    }
};

// ============================================
// INSTANCA GLOBALE - një herë, statike
// ============================================
static StratumV2Client g_sv2_client;

// ============================================
// C WRAPPER - ekspozon C++ si C
// ============================================
extern "C" {

void stratum_v2_init(const char* host, uint16_t port) {
    g_sv2_client.init(host, port);
}

void stratum_v2_connect(void) {
    g_sv2_client.connect_to_pool();
}

void stratum_v2_submit_share(uint32_t nonce, uint32_t ntime, uint32_t version) {
    g_sv2_client.submitShare(nonce, ntime, version);
}

int stratum_v2_is_connected(void) {
    return g_sv2_client.isConnected() ? 1 : 0;
}

void stratum_v2_task_loop(void) {
    g_sv2_client.loop();
}

} // extern "C"
