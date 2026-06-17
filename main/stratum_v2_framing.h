// ============================================
// FILE: stratum_v2_framing.h
// Stratum V2 Binary Message Framing
// ZERO DYNAMIC ALLOCATIONS - Static Buffers Only
// ============================================

#pragma once
#include <stdint.h>
#include <string.h>
#include <cstddef>
#include <cstdio>
#include "esp_system.h"
#include "esp_heap_caps.h"

// ============================================
// CONSTANTS
// ============================================
#define MAX_FRAME_SIZE 1024
#define MAX_PAYLOAD_SIZE (MAX_FRAME_SIZE - 6)  // 6 byte header
#define FRAME_HEADER_SIZE 6

// Stratum V2 Frame Header Structure
struct StratumV2Frame {
    uint8_t message_type;
    uint8_t channel_id;
    uint32_t sequence_number;
    uint8_t* payload;
    uint16_t payload_length;
};

// Message Type Constants
enum StratumV2MessageType : uint8_t {
    OPENING_HANDSHAKE_REQUEST = 0x00,
    OPENING_HANDSHAKE_RESPONSE = 0x01,
    CHANNEL_OPEN_REQUEST = 0x10,
    CHANNEL_OPEN_RESPONSE = 0x11,
    SETDIFF = 0x34,
    NEW_TEMPLATE = 0x21,
    SET_NEW_PREV_HASH = 0x22,
    SUBMIT_SHARES = 0x30,
};

// ============================================
// StratumV2Encoder - ZERO ALLOC VERSION
// ============================================
class StratumV2Encoder {
private:
    uint8_t frame_buffer[MAX_FRAME_SIZE];  // STATIC buffer - never reallocated
    uint32_t sequence_counter;
    size_t frame_pos;  // Current position in buffer

public:
    StratumV2Encoder() : sequence_counter(0), frame_pos(0) {
        memset(frame_buffer, 0, MAX_FRAME_SIZE);
    }

    // ============================================
    // HELPER: Initialize frame with header
    // ============================================
    void initFrame(uint8_t message_type, uint8_t channel_id) {
        frame_pos = 0;
        
        // Write header (6 bytes)
        frame_buffer[frame_pos++] = message_type;
        frame_buffer[frame_pos++] = channel_id;
        
        uint32_t seq = sequence_counter++;
        writeUint32LE(seq);  // Writes 4 bytes, increments frame_pos
    }

    // ============================================
    // HELPER: Write uint32 as little-endian
    // ============================================
    void writeUint32LE(uint32_t value) {
        if (frame_pos + 4 > MAX_FRAME_SIZE) {
            // ERROR: Buffer overflow - handle gracefully
            return;  // Caller should check frame size after
        }
        frame_buffer[frame_pos++] = (value >> 0) & 0xFF;
        frame_buffer[frame_pos++] = (value >> 8) & 0xFF;
        frame_buffer[frame_pos++] = (value >> 16) & 0xFF;
        frame_buffer[frame_pos++] = (value >> 24) & 0xFF;
    }

    // ============================================
    // HELPER: Write uint16 as little-endian
    // ============================================
    void writeUint16LE(uint16_t value) {
        if (frame_pos + 2 > MAX_FRAME_SIZE) {
            return;
        }
        frame_buffer[frame_pos++] = (value >> 0) & 0xFF;
        frame_buffer[frame_pos++] = (value >> 8) & 0xFF;
    }

    // ============================================
    // HELPER: Write byte array
    // ============================================
    void writeBytes(const uint8_t* data, size_t len) {
        if (frame_pos + len > MAX_FRAME_SIZE) {
            return;  // Overflow protection
        }
        memcpy(&frame_buffer[frame_pos], data, len);
        frame_pos += len;
    }

    // ============================================
    // HELPER: Write string with length prefix (uint16)
    // ============================================
    void writeString(const char* str) {
        uint16_t len = strlen(str);
        writeUint16LE(len);
        writeBytes((const uint8_t*)str, len);
    }

    // ============================================
    // Encode OPENING_HANDSHAKE_REQUEST
    // ============================================
    size_t encodeOpeningHandshake(
        const char* user_agent,     // "Bitaxe/1.0.0"
        const char* device_id,      // "esp32-miner-001"
        uint32_t min_version,
        uint32_t max_version
    ) {
        initFrame(OPENING_HANDSHAKE_REQUEST, 0x00);  // Control channel
        
        writeString(user_agent);
        writeString(device_id);
        writeUint32LE(min_version);
        writeUint32LE(max_version);
        
        return frame_pos;  // Return total frame size
    }

    // ============================================
    // Encode CHANNEL_OPEN_REQUEST
    // ============================================
    size_t encodeChannelOpenRequest(
        uint8_t channel_id,
        uint16_t request_id_parent,     // Parent channel request ID
        uint32_t nominal_hashrate,      // Expected hashrate (H/s)
        uint16_t max_extra_nonce2_size  // Max extra nonce size
    ) {
        initFrame(CHANNEL_OPEN_REQUEST, 0x00);  // Send on control channel
        
        writeUint16LE(request_id_parent);
        writeUint32LE(nominal_hashrate);
        writeUint16LE(max_extra_nonce2_size);
        
        return frame_pos;
    }

    // ============================================
    // Encode SET_NEW_PREV_HASH (Block template update)
    // ============================================
    size_t encodeSetNewPrevHash(
        uint8_t channel_id,
        const uint8_t* prev_hash,       // 32 bytes (Bitcoin hash)
        uint32_t header_timestamp,
        uint32_t header_nbits,
        bool coinbase_tx_included
    ) {
        initFrame(SET_NEW_PREV_HASH, channel_id);
        
        writeBytes(prev_hash, 32);      // Block hash (32 bytes)
        writeUint32LE(header_timestamp);
        writeUint32LE(header_nbits);
        frame_buffer[frame_pos++] = coinbase_tx_included ? 1 : 0;
        
        return frame_pos;
    }

    // ============================================
    // Encode SUBMIT_SHARES (Mining pool submission)
    // ============================================
    size_t encodeSubmitShares(
        uint8_t channel_id,
        uint32_t sequence_in_channel,
        uint32_t coinbase_tx_len,
        const uint8_t* coinbase_tx,
        uint32_t nonce,
        uint32_t ntime,
        uint32_t version
    ) {
        initFrame(SUBMIT_SHARES, channel_id);
        
        writeUint32LE(sequence_in_channel);
        writeUint32LE(coinbase_tx_len);
        
        // Write coinbase transaction
        if (coinbase_tx_len > 0 && coinbase_tx != nullptr) {
            writeBytes(coinbase_tx, coinbase_tx_len);
        }
        
        writeUint32LE(nonce);
        writeUint32LE(ntime);
        writeUint32LE(version);
        
        return frame_pos;
    }

    // ============================================
    // CRITICAL: Get pointer to frame data
    // ============================================
    const uint8_t* getFrameData() const {
        return frame_buffer;
    }

    // ============================================
    // CRITICAL: Get current frame size
    // ============================================
    size_t getFrameSize() const {
        return frame_pos;
    }

    // ============================================
    // Debug: Print frame hex
    // ============================================
    void debugPrintFrame(const char* label) const {
        printf("[V2] %s - Frame size: %lu bytes\n", label, (unsigned long)frame_pos);
        printf("     Hex: ");
        for (size_t i = 0; i < frame_pos && i < 64; i++) {  // Print first 64 bytes
            printf("%02X ", frame_buffer[i]);
        }
        printf("\n");
    }

    // ============================================
    // Reset for next frame
    // ============================================
    void reset() {
        frame_pos = 0;
        // DO NOT memset - waste of cycles. Just reset position.
    }
};

// ============================================
// StratumV2Decoder - Parse incoming frames
// ============================================
class StratumV2Decoder {
public:
    // ============================================
    // Parse frame header + payload
    // ============================================
    bool parseFrame(const uint8_t* data, size_t len, StratumV2Frame& out) {
        if (len < FRAME_HEADER_SIZE) {
            return false;  // Too short
        }

        out.message_type = data[0];
        out.channel_id = data[1];
        
        // Parse sequence number (little-endian)
        out.sequence_number = 
            ((uint32_t)data[2] << 0) |
            ((uint32_t)data[3] << 8) |
            ((uint32_t)data[4] << 16) |
            ((uint32_t)data[5] << 24);

        out.payload_length = len - FRAME_HEADER_SIZE;
        
        // Point payload to data after header (NO COPY!)
        out.payload = (uint8_t*)&data[FRAME_HEADER_SIZE];

        return true;
    }

    // ============================================
    // Helper: Read uint32 from payload
    // ============================================
    static uint32_t readUint32LE(const uint8_t* data, size_t offset) {
        return 
            ((uint32_t)data[offset + 0] << 0) |
            ((uint32_t)data[offset + 1] << 8) |
            ((uint32_t)data[offset + 2] << 16) |
            ((uint32_t)data[offset + 3] << 24);
    }

    // ============================================
    // Helper: Read uint16 from payload
    // ============================================
    static uint16_t readUint16LE(const uint8_t* data, size_t offset) {
        return 
            ((uint16_t)data[offset + 0] << 0) |
            ((uint16_t)data[offset + 1] << 8);
    }

    // ============================================
    // Helper: Read string with length prefix
    // ============================================
    static bool readString(const uint8_t* data, size_t offset, size_t max_len,
                          char* out_str, size_t out_size, size_t& next_offset) {
        if (offset + 2 > max_len) return false;
        
        uint16_t str_len = readUint16LE(data, offset);
        if (offset + 2 + str_len > max_len) return false;
        if (str_len >= out_size) return false;  // Buffer overflow
        
        memcpy(out_str, &data[offset + 2], str_len);
        out_str[str_len] = '\0';
        
        next_offset = offset + 2 + str_len;
        return true;
    }
};

// ============================================
// MEMORY DEBUG HELPER
// ============================================
class MemoryMonitor {
public:
    static void logHeapStatus(const char* label) {
        uint32_t free_heap = esp_get_free_heap_size();
        uint32_t min_free = esp_get_minimum_free_heap_size();
        uint32_t largest_block = heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
        
        printf("[HEAP] %s - Free: %lu bytes | Min: %lu bytes | Largest block: %lu bytes\n",
            label, (unsigned long)free_heap, (unsigned long)min_free, (unsigned long)largest_block);

        // WARNING: If free_heap < 30000, we're in danger zone
        if (free_heap < 30000) {
            printf("[HEAP] WARNING: Low memory! Risk of panic!\n");
        }
    }
};