/*
 * lora_log.h
 * Macro d'envoi LoRa calquée sur LOG_INFO
 */
#ifndef INC_LORA_LOG_H
#define INC_LORA_LOG_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdio.h>
#include "sx1262.h"
#include "log.h"

#define LORA_BUF_SIZE     64
#define LORA_TIMEOUT_MS   50

/* Usage identique à LOG_INFO : LORA("T1:%ld.%ld", i, d); */
#define LORA(fmt, ...) do { \
    char _lora_buf[LORA_BUF_SIZE]; \
    snprintf(_lora_buf, sizeof(_lora_buf), fmt, ##__VA_ARGS__); \
    SX1262_Result_t _lora_ret = SX1262_TransmitText64(_lora_buf, LORA_TIMEOUT_MS); \
    if (_lora_ret != SX1262_OK) { \
        LOG_ERROR("LoRa TX fail code=%d", _lora_ret); \
    } \
    HAL_Delay(11);\
} while(0)

#ifdef __cplusplus
}
#endif

#endif /* LORA_LOG_H */
