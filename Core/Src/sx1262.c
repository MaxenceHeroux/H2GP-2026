#include "sx1262.h"
#include "sx1262_board.h"
#include <string.h>
#include <stdbool.h>

/* -------------------------------------------------------------------------- */
/* Commandes SX1262                                                            */
/* -------------------------------------------------------------------------- */

#define SX1262_CMD_SET_SLEEP                  0x84
#define SX1262_CMD_SET_STANDBY                0x80
#define SX1262_CMD_SET_FS                     0xC1
#define SX1262_CMD_SET_TX                     0x83
#define SX1262_CMD_SET_RX                     0x82
#define SX1262_CMD_STOP_TIMER_ON_PREAMBLE     0x9F
#define SX1262_CMD_SET_RX_DUTY_CYCLE          0x94
#define SX1262_CMD_SET_CAD                    0xC5
#define SX1262_CMD_SET_TX_CONT_WAVE           0xD1
#define SX1262_CMD_SET_TX_INFINITE_PREAMBLE   0xD2
#define SX1262_CMD_SET_REGULATOR_MODE         0x96
#define SX1262_CMD_CALIBRATE                  0x89
#define SX1262_CMD_CALIBRATE_IMAGE            0x98
#define SX1262_CMD_SET_PA_CONFIG              0x95
#define SX1262_CMD_SET_RXTX_FALLBACK_MODE     0x93

#define SX1262_CMD_WRITE_REGISTER             0x0D
#define SX1262_CMD_READ_REGISTER              0x1D
#define SX1262_CMD_WRITE_BUFFER               0x0E
#define SX1262_CMD_READ_BUFFER                0x1E

#define SX1262_CMD_SET_DIO_IRQ_PARAMS         0x08
#define SX1262_CMD_GET_IRQ_STATUS             0x12
#define SX1262_CMD_CLEAR_IRQ_STATUS           0x02
#define SX1262_CMD_SET_DIO2_RF_SWITCH_CTRL    0x9D
#define SX1262_CMD_SET_DIO3_TCXO_CTRL         0x97

#define SX1262_CMD_SET_RF_FREQUENCY           0x86
#define SX1262_CMD_SET_PACKET_TYPE            0x8A
#define SX1262_CMD_GET_PACKET_TYPE            0x11
#define SX1262_CMD_SET_TX_PARAMS              0x8E
#define SX1262_CMD_SET_MODULATION_PARAMS      0x8B
#define SX1262_CMD_SET_PACKET_PARAMS          0x8C
#define SX1262_CMD_SET_BUFFER_BASE_ADDRESS    0x8F
#define SX1262_CMD_GET_RX_BUFFER_STATUS       0x13
#define SX1262_CMD_GET_PACKET_STATUS          0x14
#define SX1262_CMD_GET_RSSI_INST              0x15
#define SX1262_CMD_GET_STATUS                 0xC0

/* -------------------------------------------------------------------------- */
/* Valeurs SX1262                                                              */
/* -------------------------------------------------------------------------- */

#define SX1262_STDBY_RC                       0x00
#define SX1262_STDBY_XOSC                     0x01

#define SX1262_PACKET_TYPE_GFSK               0x00
#define SX1262_PACKET_TYPE_LORA               0x01

#define SX1262_REGULATOR_LDO                  0x00
#define SX1262_REGULATOR_DC_DC                0x01

#define SX1262_FALLBACK_STDBY_RC              0x20
#define SX1262_FALLBACK_STDBY_XOSC            0x30
#define SX1262_FALLBACK_FS                    0x40

#define SX1262_LORA_HEADER_EXPLICIT           0x00
#define SX1262_LORA_HEADER_IMPLICIT           0x01

#define SX1262_LORA_CRC_OFF                   0x00
#define SX1262_LORA_CRC_ON                    0x01

#define SX1262_LORA_IQ_NORMAL                 0x00
#define SX1262_LORA_IQ_INVERTED               0x01

#define SX1262_TX_RAMP_200_US                 0x04

#define SX1262_REG_LORA_SYNC_WORD             0x0740

/* IRQ */
#define SX1262_IRQ_TX_DONE                    0x0001
#define SX1262_IRQ_RX_DONE                    0x0002
#define SX1262_IRQ_PREAMBLE_DETECTED          0x0004
#define SX1262_IRQ_SYNC_WORD_VALID            0x0008
#define SX1262_IRQ_HEADER_VALID               0x0010
#define SX1262_IRQ_HEADER_ERR                 0x0020
#define SX1262_IRQ_CRC_ERR                    0x0040
#define SX1262_IRQ_CAD_DONE                   0x0080
#define SX1262_IRQ_CAD_DETECTED               0x0100
#define SX1262_IRQ_TIMEOUT                    0x0200
#define SX1262_IRQ_ALL                        0x03FF

/* -------------------------------------------------------------------------- */
/* État interne                                                                */
/* -------------------------------------------------------------------------- */

static SX1262_Config_t sx_cfg;
static volatile uint8_t sx1262_dio1_irq_pending = 0;

/* -------------------------------------------------------------------------- */
/* Fonctions bas niveau                                                        */
/* -------------------------------------------------------------------------- */

static SX1262_Result_t sx1262_hal_to_result(HAL_StatusTypeDef st)
{
    if (st == HAL_OK)      return SX1262_OK;
    if (st == HAL_TIMEOUT) return SX1262_TIMEOUT;
    if (st == HAL_BUSY)    return SX1262_BUSY;
    return SX1262_ERROR;
}

static SX1262_Result_t sx1262_wait_busy(uint32_t timeout_ms)
{
    uint32_t start = HAL_GetTick();

    while (SX1262_BUSY_IS_HIGH())
    {
        if ((HAL_GetTick() - start) > timeout_ms)
        {
            return SX1262_TIMEOUT;
        }
    }

    return SX1262_OK;
}

static SX1262_Result_t sx1262_write_command(uint8_t opcode, const uint8_t *params, uint8_t len)
{
    HAL_StatusTypeDef hal_st;

    SX1262_Result_t st = sx1262_wait_busy(500);
    if (st != SX1262_OK) return st;

    SX1262_NSS_LOW();

    hal_st = HAL_SPI_Transmit(&SX1262_SPI_HANDLE, &opcode, 1, 100);

    if ((hal_st == HAL_OK) && (len > 0))
    {
        hal_st = HAL_SPI_Transmit(&SX1262_SPI_HANDLE, (uint8_t *)params, len, 100);
    }

    SX1262_NSS_HIGH();

    if (hal_st != HAL_OK)
    {
        return sx1262_hal_to_result(hal_st);
    }

    return sx1262_wait_busy(500);
}

static SX1262_Result_t sx1262_read_command(uint8_t opcode, uint8_t *out, uint8_t len)
{
    uint8_t tx[260];
    uint8_t rx[260];

    if (out == NULL || len == 0 || len > 255)
    {
        return SX1262_ERROR;
    }

    memset(tx, 0x00, sizeof(tx));
    memset(rx, 0x00, sizeof(rx));

    tx[0] = opcode;

    SX1262_Result_t st = sx1262_wait_busy(500);
    if (st != SX1262_OK) return st;

    SX1262_NSS_LOW();

    HAL_StatusTypeDef hal_st = HAL_SPI_TransmitReceive(
        &SX1262_SPI_HANDLE,
        tx,
        rx,
        len + 1,
        100
    );

    SX1262_NSS_HIGH();

    if (hal_st != HAL_OK)
    {
        return sx1262_hal_to_result(hal_st);
    }

    memcpy(out, &rx[1], len);

    return SX1262_OK;
}

static SX1262_Result_t sx1262_write_register(uint16_t addr, const uint8_t *data, uint8_t len)
{
    uint8_t tx[260];

    if (data == NULL || len == 0 || len > 255)
    {
        return SX1262_ERROR;
    }

    tx[0] = SX1262_CMD_WRITE_REGISTER;
    tx[1] = (uint8_t)(addr >> 8);
    tx[2] = (uint8_t)(addr & 0xFF);

    memcpy(&tx[3], data, len);

    SX1262_Result_t st = sx1262_wait_busy(500);
    if (st != SX1262_OK) return st;

    SX1262_NSS_LOW();

    HAL_StatusTypeDef hal_st = HAL_SPI_Transmit(
        &SX1262_SPI_HANDLE,
        tx,
        len + 3,
        100
    );

    SX1262_NSS_HIGH();

    if (hal_st != HAL_OK)
    {
        return sx1262_hal_to_result(hal_st);
    }

    return sx1262_wait_busy(500);
}

static SX1262_Result_t sx1262_read_register(uint16_t addr, uint8_t *data, uint8_t len)
{
    uint8_t tx[260];
    uint8_t rx[260];

    if (data == NULL || len == 0 || len > 255)
    {
        return SX1262_ERROR;
    }

    memset(tx, 0x00, sizeof(tx));
    memset(rx, 0x00, sizeof(rx));

    tx[0] = SX1262_CMD_READ_REGISTER;
    tx[1] = (uint8_t)(addr >> 8);
    tx[2] = (uint8_t)(addr & 0xFF);
    tx[3] = 0x00;

    SX1262_Result_t st = sx1262_wait_busy(500);
    if (st != SX1262_OK) return st;

    SX1262_NSS_LOW();

    HAL_StatusTypeDef hal_st = HAL_SPI_TransmitReceive(
        &SX1262_SPI_HANDLE,
        tx,
        rx,
        len + 4,
        100
    );

    SX1262_NSS_HIGH();

    if (hal_st != HAL_OK)
    {
        return sx1262_hal_to_result(hal_st);
    }

    memcpy(data, &rx[4], len);

    return SX1262_OK;
}

static SX1262_Result_t sx1262_write_buffer(uint8_t offset, const uint8_t *data, uint8_t len)
{
    uint8_t tx[258];

    if (data == NULL || len == 0 || len > 255)
    {
        return SX1262_ERROR;
    }

    tx[0] = SX1262_CMD_WRITE_BUFFER;
    tx[1] = offset;

    memcpy(&tx[2], data, len);

    SX1262_Result_t st = sx1262_wait_busy(500);
    if (st != SX1262_OK) return st;

    SX1262_NSS_LOW();

    HAL_StatusTypeDef hal_st = HAL_SPI_Transmit(
        &SX1262_SPI_HANDLE,
        tx,
        len + 2,
        100
    );

    SX1262_NSS_HIGH();

    if (hal_st != HAL_OK)
    {
        return sx1262_hal_to_result(hal_st);
    }

    return sx1262_wait_busy(500);
}

static SX1262_Result_t sx1262_read_buffer(uint8_t offset, uint8_t *data, uint8_t len)
{
    uint8_t tx[260];
    uint8_t rx[260];

    if (data == NULL || len == 0 || len > 255)
    {
        return SX1262_ERROR;
    }

    memset(tx, 0x00, sizeof(tx));
    memset(rx, 0x00, sizeof(rx));

    tx[0] = SX1262_CMD_READ_BUFFER;
    tx[1] = offset;
    tx[2] = 0x00;

    SX1262_Result_t st = sx1262_wait_busy(500);
    if (st != SX1262_OK) return st;

    SX1262_NSS_LOW();

    HAL_StatusTypeDef hal_st = HAL_SPI_TransmitReceive(
        &SX1262_SPI_HANDLE,
        tx,
        rx,
        len + 3,
        100
    );

    SX1262_NSS_HIGH();

    if (hal_st != HAL_OK)
    {
        return sx1262_hal_to_result(hal_st);
    }

    memcpy(data, &rx[3], len);

    return SX1262_OK;
}

/* -------------------------------------------------------------------------- */
/* Commandes radio                                                             */
/* -------------------------------------------------------------------------- */

SX1262_Result_t SX1262_Reset(void)
{
    SX1262_NSS_HIGH();

    SX1262_RESET_LOW();
    HAL_Delay(2);

    SX1262_RESET_HIGH();
    HAL_Delay(20);

    return sx1262_wait_busy(1000);
}

SX1262_Result_t SX1262_GetStatus(uint8_t *status)
{
    if (status == NULL)
    {
        return SX1262_ERROR;
    }

    uint8_t rx = 0x00;

    SX1262_Result_t st = sx1262_read_command(SX1262_CMD_GET_STATUS, &rx, 1);
    if (st != SX1262_OK) return st;

    *status = rx;

    return SX1262_OK;
}

static SX1262_Result_t sx1262_set_standby(uint8_t mode)
{
    return sx1262_write_command(SX1262_CMD_SET_STANDBY, &mode, 1);
}

static SX1262_Result_t sx1262_set_regulator_dc_dc(void)
{
    uint8_t p = SX1262_REGULATOR_DC_DC;
    return sx1262_write_command(SX1262_CMD_SET_REGULATOR_MODE, &p, 1);
}

static SX1262_Result_t sx1262_set_dio2_rf_switch_ctrl(uint8_t enable)
{
    uint8_t p = enable ? 0x01 : 0x00;
    return sx1262_write_command(SX1262_CMD_SET_DIO2_RF_SWITCH_CTRL, &p, 1);
}

static SX1262_Result_t sx1262_set_dio3_tcxo_ctrl(uint8_t voltage, uint32_t delay_units)
{
    uint8_t p[4];

    p[0] = voltage;
    p[1] = (uint8_t)(delay_units >> 16);
    p[2] = (uint8_t)(delay_units >> 8);
    p[3] = (uint8_t)(delay_units);

    return sx1262_write_command(SX1262_CMD_SET_DIO3_TCXO_CTRL, p, 4);
}

static SX1262_Result_t sx1262_calibrate_image_868(void)
{
    /*
     * Calibration image pour la zone 863-870 MHz.
     */
    uint8_t p[2] = {0xD7, 0xDB};
    return sx1262_write_command(SX1262_CMD_CALIBRATE_IMAGE, p, 2);
}

static SX1262_Result_t sx1262_set_packet_type_lora(void)
{
    uint8_t p = SX1262_PACKET_TYPE_LORA;
    return sx1262_write_command(SX1262_CMD_SET_PACKET_TYPE, &p, 1);
}

static SX1262_Result_t sx1262_set_rf_frequency(uint32_t freq_hz)
{
    /*
     * Formule SX126x :
     * rf_freq = freq_hz * 2^25 / 32 MHz
     */
    uint64_t rf_freq = ((uint64_t)freq_hz << 25) / 32000000ULL;

    uint8_t p[4];

    p[0] = (uint8_t)(rf_freq >> 24);
    p[1] = (uint8_t)(rf_freq >> 16);
    p[2] = (uint8_t)(rf_freq >> 8);
    p[3] = (uint8_t)(rf_freq);

    return sx1262_write_command(SX1262_CMD_SET_RF_FREQUENCY, p, 4);
}

static SX1262_Result_t sx1262_set_pa_config_sx1262(void)
{
    /*
     * Configuration PA classique pour SX1262.
     * Convient pour démarrer à +14 dBm.
     */
    uint8_t p[4];

    p[0] = 0x04;   // paDutyCycle
    p[1] = 0x07;   // hpMax
    p[2] = 0x00;   // deviceSel = 0 pour SX1262
    p[3] = 0x01;   // paLut réservé

    return sx1262_write_command(SX1262_CMD_SET_PA_CONFIG, p, 4);
}

static SX1262_Result_t sx1262_set_tx_params(int8_t power_dbm)
{
    uint8_t p[2];

    p[0] = (uint8_t)power_dbm;
    p[1] = SX1262_TX_RAMP_200_US;

    return sx1262_write_command(SX1262_CMD_SET_TX_PARAMS, p, 2);
}

static SX1262_Result_t sx1262_set_lora_modulation_params(void)
{
    uint8_t p[4];

    p[0] = sx_cfg.spreading_factor;
    p[1] = sx_cfg.bandwidth;
    p[2] = sx_cfg.coding_rate;

    /*
     * Low Data Rate Optimize.
     * Pour SF7/BW125 : OFF.
     * Pour SF11/SF12 en BW125 : ON conseillé.
     */
    if ((sx_cfg.spreading_factor >= 11) && (sx_cfg.bandwidth == SX1262_LORA_BW_125))
    {
        p[3] = 0x01;
    }
    else
    {
        p[3] = 0x00;
    }

    return sx1262_write_command(SX1262_CMD_SET_MODULATION_PARAMS, p, 4);
}

static SX1262_Result_t sx1262_set_lora_packet_params(uint8_t payload_len)
{
    uint8_t p[6];

    p[0] = (uint8_t)(sx_cfg.preamble_len >> 8);
    p[1] = (uint8_t)(sx_cfg.preamble_len);
    p[2] = SX1262_LORA_HEADER_EXPLICIT;
    p[3] = payload_len;
    p[4] = sx_cfg.crc_on ? SX1262_LORA_CRC_ON : SX1262_LORA_CRC_OFF;
    p[5] = sx_cfg.iq_inverted ? SX1262_LORA_IQ_INVERTED : SX1262_LORA_IQ_NORMAL;

    return sx1262_write_command(SX1262_CMD_SET_PACKET_PARAMS, p, 6);
}

static SX1262_Result_t sx1262_set_buffer_base_address(uint8_t tx_base, uint8_t rx_base)
{
    uint8_t p[2];

    p[0] = tx_base;
    p[1] = rx_base;

    return sx1262_write_command(SX1262_CMD_SET_BUFFER_BASE_ADDRESS, p, 2);
}

static SX1262_Result_t sx1262_set_lora_sync_word_private(void)
{
    /*
     * Sync word LoRa privé : 0x1424.
     * Pour réseau public LoRaWAN, ce serait plutôt 0x3444.
     */
    uint8_t sw[2] = {0x14, 0x24};

    return sx1262_write_register(SX1262_REG_LORA_SYNC_WORD, sw, 2);
}

static SX1262_Result_t sx1262_set_dio_irq_params(uint16_t irq_mask,
                                                 uint16_t dio1_mask,
                                                 uint16_t dio2_mask,
                                                 uint16_t dio3_mask)
{
    uint8_t p[8];

    p[0] = (uint8_t)(irq_mask >> 8);
    p[1] = (uint8_t)(irq_mask);

    p[2] = (uint8_t)(dio1_mask >> 8);
    p[3] = (uint8_t)(dio1_mask);

    p[4] = (uint8_t)(dio2_mask >> 8);
    p[5] = (uint8_t)(dio2_mask);

    p[6] = (uint8_t)(dio3_mask >> 8);
    p[7] = (uint8_t)(dio3_mask);

    return sx1262_write_command(SX1262_CMD_SET_DIO_IRQ_PARAMS, p, 8);
}

static SX1262_Result_t sx1262_get_irq_status(uint16_t *irq)
{
    if (irq == NULL)
    {
        return SX1262_ERROR;
    }

    uint8_t rx[3];

    SX1262_Result_t st = sx1262_read_command(SX1262_CMD_GET_IRQ_STATUS, rx, 3);
    if (st != SX1262_OK) return st;

    /*
     * rx[0] = status radio
     * rx[1] = IRQ MSB
     * rx[2] = IRQ LSB
     */
    *irq = ((uint16_t)rx[1] << 8) | rx[2];

    return SX1262_OK;
}

static SX1262_Result_t sx1262_clear_irq(uint16_t irq)
{
    uint8_t p[2];

    p[0] = (uint8_t)(irq >> 8);
    p[1] = (uint8_t)(irq);

    return sx1262_write_command(SX1262_CMD_CLEAR_IRQ_STATUS, p, 2);
}

static SX1262_Result_t sx1262_set_tx(uint32_t timeout_ms)
{
    uint32_t timeout_units = timeout_ms * 64U;

    if (timeout_units > 0xFFFFFFU)
    {
        timeout_units = 0xFFFFFFU;
    }

    uint8_t p[3];

    p[0] = (uint8_t)(timeout_units >> 16);
    p[1] = (uint8_t)(timeout_units >> 8);
    p[2] = (uint8_t)(timeout_units);

    return sx1262_write_command(SX1262_CMD_SET_TX, p, 3);
}

static SX1262_Result_t sx1262_set_rx(uint32_t timeout_units)
{
    uint8_t p[3];

    p[0] = (uint8_t)(timeout_units >> 16);
    p[1] = (uint8_t)(timeout_units >> 8);
    p[2] = (uint8_t)(timeout_units);

    return sx1262_write_command(SX1262_CMD_SET_RX, p, 3);
}

static SX1262_Result_t sx1262_set_rxtx_fallback_stby_rc(void)
{
    uint8_t p = SX1262_FALLBACK_STDBY_RC;
    return sx1262_write_command(SX1262_CMD_SET_RXTX_FALLBACK_MODE, &p, 1);
}

/* -------------------------------------------------------------------------- */
/* API publique                                                                */
/* -------------------------------------------------------------------------- */

SX1262_Result_t SX1262_InitDefault(void)
{
	SX1262_Config_t cfg;

	    cfg.frequency_hz = 868000000UL;

	    /*
	     * Si les cartes sont proches, commence plutôt à 5 dBm.
	     * Pour coller à la capture Würth, tu peux remettre 14.
	     */
	    cfg.tx_power_dbm = 14;

	    cfg.spreading_factor = 5;
	    cfg.bandwidth = SX1262_LORA_BW_250;
	    cfg.coding_rate = SX1262_LORA_CR_4_5;

	    cfg.preamble_len = 8;
	    cfg.crc_on = 1;
	    cfg.iq_inverted = 0;

	    cfg.tcxo_voltage = SX1262_TCXO_1V8;

	    return SX1262_Init(&cfg);
}

SX1262_Result_t SX1262_Init(const SX1262_Config_t *cfg)
{
    SX1262_Result_t st;

    if (cfg == NULL)
    {
        return SX1262_ERROR;
    }

    sx_cfg = *cfg;

    st = SX1262_Reset();
    if (st != SX1262_OK) return st;

    st = sx1262_set_standby(SX1262_STDBY_RC);
    if (st != SX1262_OK) return st;

    /*
     * Le module Wio-SX1262 est prévu pour utiliser le régulateur DC-DC.
     */
    st = sx1262_set_regulator_dc_dc();
    if (st != SX1262_OK) return st;

    /*
     * Active le TCXO via DIO3.
     * Délai : 5 ms.
     * Unité SX1262 : 15,625 us.
     * 5 ms / 15,625 us = 320 = 0x000140.
     */
    st = sx1262_set_dio3_tcxo_ctrl(sx_cfg.tcxo_voltage, 0x000140);
    if (st != SX1262_OK) return st;

    /*
     * Passage sur oscillateur externe / TCXO.
     */
    st = sx1262_set_standby(SX1262_STDBY_XOSC);
    if (st != SX1262_OK) return st;

    /*
     * Ton RF_SW_ est tiré à 3,3 V par jumper.
     * On active quand même le contrôle automatique DIO2 du switch RF.
     * C'est indispensable pour basculer correctement TX/RX côté radio.
     */
    st = sx1262_set_dio2_rf_switch_ctrl(1);
    if (st != SX1262_OK) return st;

    /*
     * Calibration image pour 868 MHz.
     */
    st = sx1262_calibrate_image_868();
    if (st != SX1262_OK) return st;

    st = sx1262_set_packet_type_lora();
    if (st != SX1262_OK) return st;

    st = sx1262_set_rxtx_fallback_stby_rc();
    if (st != SX1262_OK) return st;

    st = sx1262_set_rf_frequency(sx_cfg.frequency_hz);
    if (st != SX1262_OK) return st;

    st = sx1262_set_pa_config_sx1262();
    if (st != SX1262_OK) return st;

    st = sx1262_set_tx_params(sx_cfg.tx_power_dbm);
    if (st != SX1262_OK) return st;

    st = sx1262_set_lora_sync_word_private();
    if (st != SX1262_OK) return st;

    st = sx1262_set_lora_modulation_params();
    if (st != SX1262_OK) return st;

    st = sx1262_set_buffer_base_address(0x00, 0x80);
    if (st != SX1262_OK) return st;

    st = sx1262_clear_irq(SX1262_IRQ_ALL);
    if (st != SX1262_OK) return st;

    return SX1262_OK;
}

SX1262_Result_t SX1262_Transmit(const uint8_t *payload, uint8_t len, uint32_t timeout_ms)
{
    SX1262_Result_t st;
    uint16_t irq;
    uint32_t start;

    if (payload == NULL || len == 0)
    {
        return SX1262_ERROR;
    }

    st = sx1262_set_standby(SX1262_STDBY_RC);
    if (st != SX1262_OK) return st;

    st = sx1262_set_lora_packet_params(len);
    if (st != SX1262_OK) return st;

    st = sx1262_set_buffer_base_address(0x00, 0x80);
    if (st != SX1262_OK) return st;

    st = sx1262_write_buffer(0x00, payload, len);
    if (st != SX1262_OK) return st;

    st = sx1262_clear_irq(SX1262_IRQ_ALL);
    if (st != SX1262_OK) return st;

    st = sx1262_set_dio_irq_params(
        SX1262_IRQ_TX_DONE | SX1262_IRQ_TIMEOUT,
        SX1262_IRQ_TX_DONE | SX1262_IRQ_TIMEOUT,
        0x0000,
        0x0000
    );
    if (st != SX1262_OK) return st;

    sx1262_dio1_irq_pending = 0;

    st = sx1262_set_tx(timeout_ms);
    if (st != SX1262_OK) return st;

    start = HAL_GetTick();

    while ((HAL_GetTick() - start) < (timeout_ms + 500U))
    {
        st = sx1262_get_irq_status(&irq);
        if (st != SX1262_OK) return st;

        if (irq & SX1262_IRQ_TX_DONE)
        {
            sx1262_clear_irq(SX1262_IRQ_ALL);
            return SX1262_OK;
        }

        if (irq & SX1262_IRQ_TIMEOUT)
        {
            sx1262_clear_irq(SX1262_IRQ_ALL);
            return SX1262_TIMEOUT;
        }

        HAL_Delay(1);
    }

    sx1262_clear_irq(SX1262_IRQ_ALL);
    return SX1262_TIMEOUT;
}

SX1262_Result_t SX1262_StartRxContinuous(uint8_t max_payload_len)
{
    SX1262_Result_t st;

    st = sx1262_set_standby(SX1262_STDBY_RC);
    if (st != SX1262_OK) return st;

    st = sx1262_set_lora_packet_params(max_payload_len);
    if (st != SX1262_OK) return st;

    st = sx1262_set_buffer_base_address(0x00, 0x80);
    if (st != SX1262_OK) return st;

    st = sx1262_clear_irq(SX1262_IRQ_ALL);
    if (st != SX1262_OK) return st;

    st = sx1262_set_dio_irq_params(
        SX1262_IRQ_RX_DONE | SX1262_IRQ_TIMEOUT | SX1262_IRQ_CRC_ERR,
        SX1262_IRQ_RX_DONE | SX1262_IRQ_TIMEOUT | SX1262_IRQ_CRC_ERR,
        0x0000,
        0x0000
    );
    if (st != SX1262_OK) return st;

    sx1262_dio1_irq_pending = 0;

    /*
     * 0xFFFFFF = réception continue.
     */
    return sx1262_set_rx(0xFFFFFF);
}

SX1262_Result_t SX1262_ReadReceived(uint8_t *buffer, uint8_t buffer_size, uint8_t *received_len)
{
    SX1262_Result_t st;
    uint16_t irq;
    uint8_t rx_status[3];

    if (buffer == NULL || received_len == NULL)
    {
        return SX1262_ERROR;
    }

    *received_len = 0;

    st = sx1262_get_irq_status(&irq);
    if (st != SX1262_OK) return st;

    if ((irq & SX1262_IRQ_RX_DONE) == 0)
    {
        return SX1262_BUSY;
    }

    if (irq & SX1262_IRQ_CRC_ERR)
    {
        sx1262_clear_irq(SX1262_IRQ_ALL);
        return SX1262_CRC_ERROR;
    }

    /*
     * GetRxBufferStatus retourne :
     * rx_status[0] = status radio
     * rx_status[1] = payload length
     * rx_status[2] = rx start buffer pointer
     */
    st = sx1262_read_command(SX1262_CMD_GET_RX_BUFFER_STATUS, rx_status, 3);
    if (st != SX1262_OK) return st;

    uint8_t payload_len = rx_status[1];
    uint8_t start_pointer = rx_status[2];

    if (payload_len > buffer_size)
    {
        sx1262_clear_irq(SX1262_IRQ_ALL);
        return SX1262_ERROR;
    }

    st = sx1262_read_buffer(start_pointer, buffer, payload_len);
    if (st != SX1262_OK) return st;

    *received_len = payload_len;

    sx1262_clear_irq(SX1262_IRQ_ALL);
    sx1262_dio1_irq_pending = 0;

    return SX1262_OK;
}

/* -------------------------------------------------------------------------- */
/* Gestion IRQ DIO1                                                            */
/* -------------------------------------------------------------------------- */

void SX1262_OnDio1Irq(void)
{
    sx1262_dio1_irq_pending = 1;
}

uint8_t SX1262_IsDio1IrqPending(void)
{
    return sx1262_dio1_irq_pending;
}

void SX1262_ClearDio1IrqPending(void)
{
    sx1262_dio1_irq_pending = 0;
}


SX1262_Result_t SX1262_TransmitText(const char *text, uint32_t timeout_ms)
{
    if (text == NULL)
    {
        return SX1262_ERROR;
    }

    size_t len = strlen(text);

    if (len == 0)
    {
        return SX1262_ERROR;
    }

    if (len > 255)
    {
        len = 255;
    }

    return SX1262_Transmit((const uint8_t *)text, (uint8_t)len, timeout_ms);
}

SX1262_Result_t SX1262_TransmitText64(const char *text, uint32_t timeout_ms)
{
    uint8_t payload[64];
    size_t len;

    if (text == NULL)
    {
        return SX1262_ERROR;
    }

    memset(payload, ' ', sizeof(payload));

    len = strlen(text);

    if (len > 64)
    {
        len = 64;
    }

    memcpy(payload, text, len);

    return SX1262_Transmit(payload, 64, timeout_ms);
}
