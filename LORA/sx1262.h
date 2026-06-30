#ifndef SX1262_H
#define SX1262_H

#include "main.h"
#include <stdint.h>

typedef enum
{
    SX1262_OK = 0,
    SX1262_ERROR,
    SX1262_TIMEOUT,
    SX1262_BUSY,
    SX1262_CRC_ERROR
} SX1262_Result_t;

typedef struct
{
    uint32_t frequency_hz;
    int8_t tx_power_dbm;

    uint8_t spreading_factor;
    uint8_t bandwidth;
    uint8_t coding_rate;

    uint16_t preamble_len;
    uint8_t crc_on;
    uint8_t iq_inverted;

    uint8_t tcxo_voltage;
} SX1262_Config_t;

#define SX1262_TCXO_1V6     0x00
#define SX1262_TCXO_1V7     0x01
#define SX1262_TCXO_1V8     0x02
#define SX1262_TCXO_2V2     0x03
#define SX1262_TCXO_2V4     0x04
#define SX1262_TCXO_2V7     0x05
#define SX1262_TCXO_3V0     0x06
#define SX1262_TCXO_3V3     0x07

#define SX1262_LORA_BW_125  0x04
#define SX1262_LORA_BW_250  0x05
#define SX1262_LORA_BW_500  0x06

#define SX1262_LORA_CR_4_5  0x01
#define SX1262_LORA_CR_4_6  0x02
#define SX1262_LORA_CR_4_7  0x03
#define SX1262_LORA_CR_4_8  0x04

SX1262_Result_t SX1262_InitDefault(void);
SX1262_Result_t SX1262_Init(const SX1262_Config_t *cfg);

SX1262_Result_t SX1262_Reset(void);
SX1262_Result_t SX1262_GetStatus(uint8_t *status);

SX1262_Result_t SX1262_Transmit(const uint8_t *payload, uint8_t len, uint32_t timeout_ms);

SX1262_Result_t SX1262_StartRxContinuous(uint8_t max_payload_len);
SX1262_Result_t SX1262_ReadReceived(uint8_t *buffer, uint8_t buffer_size, uint8_t *received_len);

void SX1262_OnDio1Irq(void);
uint8_t SX1262_IsDio1IrqPending(void);
void SX1262_ClearDio1IrqPending(void);

SX1262_Result_t SX1262_StartContinuousWave(void);
SX1262_Result_t SX1262_StopRadio(void);

SX1262_Result_t SX1262_TransmitText(const char *text, uint32_t timeout_ms);
SX1262_Result_t SX1262_TransmitText64(const char *text, uint32_t timeout_ms);

#endif
