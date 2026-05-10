// ============================================
// FILE: main/stratum_v2_client.h
// ============================================
#pragma once
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void stratum_v2_init(const char* host, uint16_t port);
void stratum_v2_connect(void);
void stratum_v2_submit_share(uint32_t nonce, uint32_t ntime, uint32_t version);
int  stratum_v2_is_connected(void);
void stratum_v2_task_loop(void);

#ifdef __cplusplus
}
#endif