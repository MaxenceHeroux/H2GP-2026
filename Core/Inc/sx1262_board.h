#ifndef SX1262_BOARD_H
#define SX1262_BOARD_H

#include "main.h"
#include "spi.h"

extern SPI_HandleTypeDef hspi1;

/*
 * Mapping exact de tes noms CubeMX :
 *
 * SPI1_NSS    -> PA4
 * SPI1_SCK    -> PA5
 * SPI1_MISO   -> PB4
 * SPI1_MOSI   -> PB5
 * LORA_NRST   -> PB12
 * GPIO_BUSY   -> PB2
 * GPIO_EXTI13 -> PB13 / DIO1
 */

#define SX1262_SPI_HANDLE       hspi1

#define SX1262_NSS_PORT         SPI1_NSS_GPIO_Port
#define SX1262_NSS_PIN          SPI1_NSS_Pin

#define SX1262_NRST_PORT        LORA_NRST_GPIO_Port
#define SX1262_NRST_PIN         LORA_NRST_Pin

#define SX1262_BUSY_PORT        GPIO_BUSY_GPIO_Port
#define SX1262_BUSY_PIN         GPIO_BUSY_Pin

#define SX1262_DIO1_PORT        GPIO_EXTI13_GPIO_Port
#define SX1262_DIO1_PIN         GPIO_EXTI13_Pin

#define SX1262_NSS_LOW()        HAL_GPIO_WritePin(SX1262_NSS_PORT, SX1262_NSS_PIN, GPIO_PIN_RESET)
#define SX1262_NSS_HIGH()       HAL_GPIO_WritePin(SX1262_NSS_PORT, SX1262_NSS_PIN, GPIO_PIN_SET)

#define SX1262_RESET_LOW()      HAL_GPIO_WritePin(SX1262_NRST_PORT, SX1262_NRST_PIN, GPIO_PIN_RESET)
#define SX1262_RESET_HIGH()     HAL_GPIO_WritePin(SX1262_NRST_PORT, SX1262_NRST_PIN, GPIO_PIN_SET)

#define SX1262_BUSY_IS_HIGH()   (HAL_GPIO_ReadPin(SX1262_BUSY_PORT, SX1262_BUSY_PIN) == GPIO_PIN_SET)

#endif
