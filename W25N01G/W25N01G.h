#ifndef W25N01G_H
#define W25N01G_H

#include "main.h"  // pour QSPI_HandleTypeDef (adapte si besoin)
#include <stdint.h>
#include <stdbool.h>

/* ===================== OPCODES ===================== */
#define W25N_CMD_RESET              0xFF
#define W25N_CMD_JEDEC_ID           0x9F
#define W25N_CMD_READ_SR            0x0F   /* ou 0x05, équivalent */
#define W25N_CMD_WRITE_SR           0x1F   /* ou 0x01, équivalent */
#define W25N_CMD_WRITE_ENABLE       0x06
#define W25N_CMD_WRITE_DISABLE      0x04
#define W25N_CMD_BB_MANAGE          0xA1
#define W25N_CMD_READ_BBM_LUT       0xA5
#define W25N_CMD_LAST_ECC_FAIL_PAGE 0xA9
#define W25N_CMD_BLOCK_ERASE        0xD8
#define W25N_CMD_PROGRAM_LOAD       0x02
#define W25N_CMD_PROGRAM_LOAD_RAND  0x84
#define W25N_CMD_PROGRAM_EXECUTE    0x10
#define W25N_CMD_PAGE_DATA_READ     0x13
#define W25N_CMD_READ               0x03
#define W25N_CMD_FAST_READ          0x0B

/* ===================== STATUS REGISTER ADDRESSES ===================== */
#define W25N_SR1_ADDR  0xA0   /* Protection Register */
#define W25N_SR2_ADDR  0xB0   /* Configuration Register */
#define W25N_SR3_ADDR  0xC0   /* Status Register (status only) */

/* ===================== SR-1 (Protection) BITS ===================== */
#define W25N_SR1_SRP1   (1 << 7)
#define W25N_SR1_BP3    (1 << 6)
#define W25N_SR1_BP2    (1 << 5)
#define W25N_SR1_BP1    (1 << 4)
#define W25N_SR1_BP0    (1 << 3)
#define W25N_SR1_TB     (1 << 2)
#define W25N_SR1_WP_E   (1 << 1)
#define W25N_SR1_SRP0   (1 << 0)

/* ===================== SR-2 (Configuration) BITS ===================== */
#define W25N_SR2_OTP_L  (1 << 7)
#define W25N_SR2_OTP_E  (1 << 6)
#define W25N_SR2_SR1_L  (1 << 5)
#define W25N_SR2_ECC_E  (1 << 4)
#define W25N_SR2_BUF    (1 << 3)

/* ===================== SR-3 (Status) BITS ===================== */
#define W25N_SR3_LUT_F  (1 << 6)
#define W25N_SR3_ECC1   (1 << 5)
#define W25N_SR3_ECC0   (1 << 4)
#define W25N_SR3_PFAIL  (1 << 3)
#define W25N_SR3_EFAIL  (1 << 2)
#define W25N_SR3_WEL    (1 << 1)
#define W25N_SR3_BUSY   (1 << 0)

/* ===================== GEOMETRY ===================== */
#define W25N_PAGE_SIZE         2048    /* zone utilisateur, sans le spare */
#define W25N_PAGE_SIZE_TOTAL   2112    /* avec les 64 octets de spare/ECC */
#define W25N_PAGES_PER_BLOCK   64
#define W25N_BLOCK_COUNT       1024
#define W25N_TOTAL_PAGES       (W25N_PAGES_PER_BLOCK * W25N_BLOCK_COUNT) /* 65536 */

/* JEDEC ID attendu */
#define W25N_MANUFACTURER_ID   0xEF
#define W25N_DEVICE_ID         0xAA21

/* ===================== TYPES ===================== */
typedef enum {
    W25N_OK = 0,
    W25N_ERROR_HAL,
    W25N_ERROR_TIMEOUT,
    W25N_ERROR_ID_MISMATCH,
    W25N_ERROR_PROGRAM_FAIL,
    W25N_ERROR_ERASE_FAIL,
    W25N_ERROR_ECC_UNCORRECTABLE,
    W25N_ERROR_PARAM
} W25N_StatusTypeDef;

typedef struct {
    QSPI_HandleTypeDef *hqspi;
} W25N_HandleTypeDef;

/* ===================== API ===================== */
W25N_StatusTypeDef W25N_Init(W25N_HandleTypeDef *dev, QSPI_HandleTypeDef *hqspi);
W25N_StatusTypeDef W25N_Reset(W25N_HandleTypeDef *dev);
W25N_StatusTypeDef W25N_ReadJedecId(W25N_HandleTypeDef *dev, uint8_t *manufacturer_id, uint16_t *device_id);

W25N_StatusTypeDef W25N_ReadStatusReg(W25N_HandleTypeDef *dev, uint8_t reg_addr, uint8_t *value);
W25N_StatusTypeDef W25N_WriteStatusReg(W25N_HandleTypeDef *dev, uint8_t reg_addr, uint8_t value);

W25N_StatusTypeDef W25N_WriteEnable(W25N_HandleTypeDef *dev);
W25N_StatusTypeDef W25N_WriteDisable(W25N_HandleTypeDef *dev);

W25N_StatusTypeDef W25N_WaitBusy(W25N_HandleTypeDef *dev, uint32_t timeout_ms);
W25N_StatusTypeDef W25N_IsBusy(W25N_HandleTypeDef *dev, bool *busy);

W25N_StatusTypeDef W25N_BlockErase(W25N_HandleTypeDef *dev, uint16_t page_address);

W25N_StatusTypeDef W25N_ProgramLoad(W25N_HandleTypeDef *dev, uint16_t column_address, const uint8_t *data, uint16_t len);
W25N_StatusTypeDef W25N_ProgramExecute(W25N_HandleTypeDef *dev, uint16_t page_address);

W25N_StatusTypeDef W25N_PageDataRead(W25N_HandleTypeDef *dev, uint16_t page_address);
W25N_StatusTypeDef W25N_ReadData(W25N_HandleTypeDef *dev, uint16_t column_address, uint8_t *buffer, uint32_t len);

/* Fonctions haut niveau combinant les étapes */
W25N_StatusTypeDef W25N_WritePage(W25N_HandleTypeDef *dev, uint16_t page_address, const uint8_t *data, uint16_t len);
W25N_StatusTypeDef W25N_ReadPage(W25N_HandleTypeDef *dev, uint16_t page_address, uint8_t *buffer, uint16_t len);
W25N_StatusTypeDef W25N_EraseBlock(W25N_HandleTypeDef *dev, uint16_t block_number);

#endif /* W25N01G_H */
