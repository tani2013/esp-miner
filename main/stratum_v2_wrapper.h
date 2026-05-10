// ============================================
// FILE: main/stratum_v2_wrapper.h
// Ura lidhëse: C (main.c) <-> C++ (stratum_v2_client.cpp)
// ============================================
#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Inicializo lidhjen Stratum V2
 * Thirret një herë nga stratum_task.c
 */
void stratum_v2_init(const char* pool_host, uint16_t pool_port);

/**
 * Fillo lidhjen TCP me pishinën
 * Thirret pas WiFi connect
 */
void stratum_v2_connect(void);

/**
 * Dërgo një share të gërmuar në pishinë
 * Thirret nga asic_result_task.c kur ASIC-u gjen një share
 */
void stratum_v2_submit_share(uint32_t nonce, uint32_t ntime, uint32_t version);

/**
 * Kontrollo nëse jemi të lidhur me pishinën
 * Kthehet: 1 = i lidhur, 0 = i shkëputur
 */
int stratum_v2_is_connected(void);

/**
 * Loop-i kryesor i V2 - thirret çdo 10ms nga task-u
 * Menaxhon reconnect, heartbeat, receive
 */
void stratum_v2_task_loop(void);

#ifdef __cplusplus
}
#endif
