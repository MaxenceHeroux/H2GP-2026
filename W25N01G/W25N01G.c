#include "W25N01G.h"
#include <string.h>

/* ============================================================
 * Helpers internes bas niveau QSPI
 * ============================================================ */

/* Commande simple sans adresse ni donnée (ex: Reset, Write Enable) */
static W25N_StatusTypeDef W25N_SendCmdOnly(W25N_HandleTypeDef *dev, uint8_t instruction)
{
    QSPI_CommandTypeDef cmd = {0};
    cmd.InstructionMode = QSPI_INSTRUCTION_1_LINE;
    cmd.Instruction     = instruction;
    cmd.AddressMode     = QSPI_ADDRESS_NONE;
    cmd.AlternateByteMode = QSPI_ALTERNATE_BYTES_NONE;
    cmd.DataMode        = QSPI_DATA_NONE;
    cmd.DummyCycles     = 0;
    cmd.DdrMode         = QSPI_DDR_MODE_DISABLE;
    cmd.DdrHoldHalfCycle = QSPI_DDR_HHC_ANALOG_DELAY;
    cmd.SIOOMode        = QSPI_SIOO_INST_EVERY_CMD;

    if (HAL_QSPI_Command(dev->hqspi, &cmd, 100) != HAL_OK) {
        return W25N_ERROR_HAL;
    }
    return W25N_OK;
}

/* Commande avec 1 octet d'adresse (registre) + écriture d'1 octet de donnée */
static W25N_StatusTypeDef W25N_WriteRegByte(W25N_HandleTypeDef *dev, uint8_t instruction, uint8_t reg_addr, uint8_t value)
{
    QSPI_CommandTypeDef cmd = {0};
    cmd.InstructionMode = QSPI_INSTRUCTION_1_LINE;
    cmd.Instruction     = instruction;
    cmd.AddressMode     = QSPI_ADDRESS_1_LINE;
    cmd.AddressSize     = QSPI_ADDRESS_8_BITS;
    cmd.Address         = reg_addr;
    cmd.AlternateByteMode = QSPI_ALTERNATE_BYTES_NONE;
    cmd.DataMode        = QSPI_DATA_1_LINE;
    cmd.NbData          = 1;
    cmd.DummyCycles     = 0;
    cmd.DdrMode         = QSPI_DDR_MODE_DISABLE;
    cmd.DdrHoldHalfCycle = QSPI_DDR_HHC_ANALOG_DELAY;
    cmd.SIOOMode        = QSPI_SIOO_INST_EVERY_CMD;

    if (HAL_QSPI_Command(dev->hqspi, &cmd, 100) != HAL_OK) {
        return W25N_ERROR_HAL;
    }
    if (HAL_QSPI_Transmit(dev->hqspi, &value, 100) != HAL_OK) {
        return W25N_ERROR_HAL;
    }
    return W25N_OK;
}

/* Commande avec 1 octet d'adresse (registre) + lecture d'1 octet de donnée */
static W25N_StatusTypeDef W25N_ReadRegByte(W25N_HandleTypeDef *dev, uint8_t instruction, uint8_t reg_addr, uint8_t *value)
{
    QSPI_CommandTypeDef cmd = {0};
    cmd.InstructionMode = QSPI_INSTRUCTION_1_LINE;
    cmd.Instruction     = instruction;
    cmd.AddressMode     = QSPI_ADDRESS_1_LINE;
    cmd.AddressSize     = QSPI_ADDRESS_8_BITS;
    cmd.Address         = reg_addr;
    cmd.AlternateByteMode = QSPI_ALTERNATE_BYTES_NONE;
    cmd.DataMode        = QSPI_DATA_1_LINE;
    cmd.NbData          = 1;
    cmd.DummyCycles     = 0;
    cmd.DdrMode         = QSPI_DDR_MODE_DISABLE;
    cmd.DdrHoldHalfCycle = QSPI_DDR_HHC_ANALOG_DELAY;
    cmd.SIOOMode        = QSPI_SIOO_INST_EVERY_CMD;

    if (HAL_QSPI_Command(dev->hqspi, &cmd, 100) != HAL_OK) {
        return W25N_ERROR_HAL;
    }
    if (HAL_QSPI_Receive(dev->hqspi, value, 100) != HAL_OK) {
        return W25N_ERROR_HAL;
    }
    return W25N_OK;
}

/* Commande avec adresse 16 bits (page ou colonne) + 8 dummy cycles, sans donnée
 * Utilisée pour : Page Data Read (13h), Block Erase (D8h), Program Execute (10h)
 * Format datasheet : OpCode, Dummy(8bits), PA[15:8], PA[7:0]
 * On modélise le "Dummy byte" comme faisant partie de l'adresse 24 bits envoyée en 1 ligne.
 */
static W25N_StatusTypeDef W25N_SendCmdWithPageAddr(W25N_HandleTypeDef *dev, uint8_t instruction, uint16_t page_address)
{
    QSPI_CommandTypeDef cmd = {0};
    cmd.InstructionMode = QSPI_INSTRUCTION_1_LINE;
    cmd.Instruction     = instruction;
    cmd.AddressMode     = QSPI_ADDRESS_1_LINE;
    cmd.AddressSize     = QSPI_ADDRESS_24_BITS;   /* 8 bits dummy + 16 bits page address */
    cmd.Address         = ((uint32_t)page_address) & 0x00FFFFUL; /* le dummy byte = 0x00 en MSB */
    cmd.AlternateByteMode = QSPI_ALTERNATE_BYTES_NONE;
    cmd.DataMode        = QSPI_DATA_NONE;
    cmd.DummyCycles     = 0;
    cmd.DdrMode         = QSPI_DDR_MODE_DISABLE;
    cmd.DdrHoldHalfCycle = QSPI_DDR_HHC_ANALOG_DELAY;
    cmd.SIOOMode        = QSPI_SIOO_INST_EVERY_CMD;

    if (HAL_QSPI_Command(dev->hqspi, &cmd, 100) != HAL_OK) {
        return W25N_ERROR_HAL;
    }
    return W25N_OK;
}

/* ============================================================
 * API publique
 * ============================================================ */

W25N_StatusTypeDef W25N_Init(W25N_HandleTypeDef *dev, QSPI_HandleTypeDef *hqspi)
{
    if (dev == NULL || hqspi == NULL) {
        return W25N_ERROR_PARAM;
    }
    dev->hqspi = hqspi;

    /* Reset logiciel pour repartir d'un état propre */
    W25N_StatusTypeDef st = W25N_Reset(dev);
    if (st != W25N_OK) return st;

    HAL_Delay(1); /* tRST max ~500us, on prend large */

    /* Vérifie l'identité du chip avant d'aller plus loin */
    uint8_t mfr = 0;
    uint16_t devid = 0;
    st = W25N_ReadJedecId(dev, &mfr, &devid);
    if (st != W25N_OK) return st;

    if (mfr != W25N_MANUFACTURER_ID || devid != W25N_DEVICE_ID) {
        return W25N_ERROR_ID_MISMATCH;
    }

    return W25N_OK;
}

W25N_StatusTypeDef W25N_Reset(W25N_HandleTypeDef *dev)
{
    return W25N_SendCmdOnly(dev, W25N_CMD_RESET);
}

W25N_StatusTypeDef W25N_ReadJedecId(W25N_HandleTypeDef *dev, uint8_t *manufacturer_id, uint16_t *device_id)
{
    QSPI_CommandTypeDef cmd = {0};
    uint8_t rx[3] = {0};

    cmd.InstructionMode = QSPI_INSTRUCTION_1_LINE;
    cmd.Instruction     = W25N_CMD_JEDEC_ID;
    cmd.AddressMode     = QSPI_ADDRESS_NONE;
    cmd.AlternateByteMode = QSPI_ALTERNATE_BYTES_NONE;
    cmd.DataMode        = QSPI_DATA_1_LINE;
    cmd.NbData          = 3; /* dummy géré séparément : datasheet veut 8 dummy clocks puis EFh, AAh, 21h */
    cmd.DummyCycles     = 8;
    cmd.DdrMode         = QSPI_DDR_MODE_DISABLE;
    cmd.DdrHoldHalfCycle = QSPI_DDR_HHC_ANALOG_DELAY;
    cmd.SIOOMode        = QSPI_SIOO_INST_EVERY_CMD;

    if (HAL_QSPI_Command(dev->hqspi, &cmd, 100) != HAL_OK) {
        return W25N_ERROR_HAL;
    }
    if (HAL_QSPI_Receive(dev->hqspi, rx, 100) != HAL_OK) {
        return W25N_ERROR_HAL;
    }

    *manufacturer_id = rx[0];
    *device_id = ((uint16_t)rx[1] << 8) | rx[2];
    return W25N_OK;
}

W25N_StatusTypeDef W25N_ReadStatusReg(W25N_HandleTypeDef *dev, uint8_t reg_addr, uint8_t *value)
{
    return W25N_ReadRegByte(dev, W25N_CMD_READ_SR, reg_addr, value);
}

W25N_StatusTypeDef W25N_WriteStatusReg(W25N_HandleTypeDef *dev, uint8_t reg_addr, uint8_t value)
{
    return W25N_WriteRegByte(dev, W25N_CMD_WRITE_SR, reg_addr, value);
}

W25N_StatusTypeDef W25N_WriteEnable(W25N_HandleTypeDef *dev)
{
    return W25N_SendCmdOnly(dev, W25N_CMD_WRITE_ENABLE);
}

W25N_StatusTypeDef W25N_WriteDisable(W25N_HandleTypeDef *dev)
{
    return W25N_SendCmdOnly(dev, W25N_CMD_WRITE_DISABLE);
}

W25N_StatusTypeDef W25N_IsBusy(W25N_HandleTypeDef *dev, bool *busy)
{
    uint8_t sr3 = 0;
    W25N_StatusTypeDef st = W25N_ReadStatusReg(dev, W25N_SR3_ADDR, &sr3);
    if (st != W25N_OK) return st;
    *busy = (sr3 & W25N_SR3_BUSY) != 0;
    return W25N_OK;
}

W25N_StatusTypeDef W25N_WaitBusy(W25N_HandleTypeDef *dev, uint32_t timeout_ms)
{
    uint32_t start = HAL_GetTick();
    bool busy = true;

    while (busy) {
        W25N_StatusTypeDef st = W25N_IsBusy(dev, &busy);
        if (st != W25N_OK) return st;

        if (!busy) break;

        if ((HAL_GetTick() - start) > timeout_ms) {
            return W25N_ERROR_TIMEOUT;
        }
    }
    return W25N_OK;
}

W25N_StatusTypeDef W25N_BlockErase(W25N_HandleTypeDef *dev, uint16_t page_address)
{
    W25N_StatusTypeDef st = W25N_WriteEnable(dev);
    if (st != W25N_OK) return st;

    st = W25N_SendCmdWithPageAddr(dev, W25N_CMD_BLOCK_ERASE, page_address);
    if (st != W25N_OK) return st;

    st = W25N_WaitBusy(dev, 500); /* tBE typique ~2-10ms, on prend large */
    if (st != W25N_OK) return st;

    /* Vérifie le bit E_FAIL */
    uint8_t sr3 = 0;
    st = W25N_ReadStatusReg(dev, W25N_SR3_ADDR, &sr3);
    if (st != W25N_OK) return st;
    if (sr3 & W25N_SR3_EFAIL) {
        return W25N_ERROR_ERASE_FAIL;
    }
    return W25N_OK;
}

W25N_StatusTypeDef W25N_ProgramLoad(W25N_HandleTypeDef *dev, uint16_t column_address, const uint8_t *data, uint16_t len)
{
    QSPI_CommandTypeDef cmd = {0};
    cmd.InstructionMode = QSPI_INSTRUCTION_1_LINE;
    cmd.Instruction     = W25N_CMD_PROGRAM_LOAD;
    cmd.AddressMode     = QSPI_ADDRESS_1_LINE;
    cmd.AddressSize     = QSPI_ADDRESS_16_BITS;
    cmd.Address         = column_address;
    cmd.AlternateByteMode = QSPI_ALTERNATE_BYTES_NONE;
    cmd.DataMode        = QSPI_DATA_1_LINE;
    cmd.NbData          = len;
    cmd.DummyCycles     = 0;
    cmd.DdrMode         = QSPI_DDR_MODE_DISABLE;
    cmd.DdrHoldHalfCycle = QSPI_DDR_HHC_ANALOG_DELAY;
    cmd.SIOOMode        = QSPI_SIOO_INST_EVERY_CMD;

    if (HAL_QSPI_Command(dev->hqspi, &cmd, 100) != HAL_OK) {
        return W25N_ERROR_HAL;
    }
    if (HAL_QSPI_Transmit(dev->hqspi, (uint8_t *)data, 1000) != HAL_OK) {
        return W25N_ERROR_HAL;
    }
    return W25N_OK;
}

W25N_StatusTypeDef W25N_ProgramExecute(W25N_HandleTypeDef *dev, uint16_t page_address)
{
    W25N_StatusTypeDef st = W25N_SendCmdWithPageAddr(dev, W25N_CMD_PROGRAM_EXECUTE, page_address);
    if (st != W25N_OK) return st;

    st = W25N_WaitBusy(dev, 50); /* tPP typique ~0.2-1.5ms */
    if (st != W25N_OK) return st;

    /* Vérifie le bit P_FAIL */
    uint8_t sr3 = 0;
    st = W25N_ReadStatusReg(dev, W25N_SR3_ADDR, &sr3);
    if (st != W25N_OK) return st;
    if (sr3 & W25N_SR3_PFAIL) {
        return W25N_ERROR_PROGRAM_FAIL;
    }
    return W25N_OK;
}

W25N_StatusTypeDef W25N_PageDataRead(W25N_HandleTypeDef *dev, uint16_t page_address)
{
    W25N_StatusTypeDef st = W25N_SendCmdWithPageAddr(dev, W25N_CMD_PAGE_DATA_READ, page_address);
    if (st != W25N_OK) return st;

    st = W25N_WaitBusy(dev, 10); /* tRD typique ~25-60us */
    if (st != W25N_OK) return st;

    return W25N_OK;
}

W25N_StatusTypeDef W25N_ReadData(W25N_HandleTypeDef *dev, uint16_t column_address, uint8_t *buffer, uint32_t len)
{
    QSPI_CommandTypeDef cmd = {0};
    cmd.InstructionMode = QSPI_INSTRUCTION_1_LINE;
    cmd.Instruction     = W25N_CMD_READ;
    cmd.AddressMode     = QSPI_ADDRESS_1_LINE;
    cmd.AddressSize     = QSPI_ADDRESS_16_BITS;
    cmd.Address         = column_address;
    cmd.AlternateByteMode = QSPI_ALTERNATE_BYTES_NONE;
    cmd.DataMode        = QSPI_DATA_1_LINE;
    cmd.NbData          = len;
    cmd.DummyCycles     = 8; /* datasheet : 8 dummy clocks après l'adresse, mode buffer read */
    cmd.DdrMode         = QSPI_DDR_MODE_DISABLE;
    cmd.DdrHoldHalfCycle = QSPI_DDR_HHC_ANALOG_DELAY;
    cmd.SIOOMode        = QSPI_SIOO_INST_EVERY_CMD;

    if (HAL_QSPI_Command(dev->hqspi, &cmd, 100) != HAL_OK) {
        return W25N_ERROR_HAL;
    }
    if (HAL_QSPI_Receive(dev->hqspi, buffer, 1000) != HAL_OK) {
        return W25N_ERROR_HAL;
    }
    return W25N_OK;
}

/* ============================================================
 * Fonctions haut niveau (combinent les étapes bas niveau)
 * ============================================================ */

W25N_StatusTypeDef W25N_WritePage(W25N_HandleTypeDef *dev, uint16_t page_address, const uint8_t *data, uint16_t len)
{
    if (len > W25N_PAGE_SIZE) return W25N_ERROR_PARAM;

    W25N_StatusTypeDef st = W25N_WriteEnable(dev);
    if (st != W25N_OK) return st;

    st = W25N_ProgramLoad(dev, 0, data, len);
    if (st != W25N_OK) return st;

    st = W25N_ProgramExecute(dev, page_address);
    if (st != W25N_OK) return st;

    return W25N_OK;
}

W25N_StatusTypeDef W25N_ReadPage(W25N_HandleTypeDef *dev, uint16_t page_address, uint8_t *buffer, uint16_t len)
{
    if (len > W25N_PAGE_SIZE) return W25N_ERROR_PARAM;

    W25N_StatusTypeDef st = W25N_PageDataRead(dev, page_address);
    if (st != W25N_OK) return st;

    st = W25N_ReadData(dev, 0, buffer, len);
    if (st != W25N_OK) return st;

    /* Vérifie l'ECC après lecture */
    uint8_t sr3 = 0;
    st = W25N_ReadStatusReg(dev, W25N_SR3_ADDR, &sr3);
    if (st != W25N_OK) return st;

    uint8_t ecc = (sr3 & (W25N_SR3_ECC1 | W25N_SR3_ECC0)) >> 4;
    if (ecc == 0x2 || ecc == 0x3) {
        /* erreurs non corrigibles */
        return W25N_ERROR_ECC_UNCORRECTABLE;
    }
    return W25N_OK;
}

W25N_StatusTypeDef W25N_EraseBlock(W25N_HandleTypeDef *dev, uint16_t block_number)
{
    /* Page address pour Block Erase : n'importe quelle page du bloc fonctionne,
       on utilise la première page du bloc : block_number * pages_per_block */
    uint16_t page_address = block_number * W25N_PAGES_PER_BLOCK;
    return W25N_BlockErase(dev, page_address);
}
