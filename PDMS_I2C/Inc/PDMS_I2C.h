/*
 ***************************************************************************************************
 * This file is part of Sensors SDK:
 * https://www.we-online.com/sensors, https://github.com/WurthElektronik/Sensors-SDK_STM32
 *
 * THE SOFTWARE INCLUDING THE SOURCE CODE IS PROVIDED “AS IS”. YOU ACKNOWLEDGE THAT WÜRTH ELEKTRONIK
 * EISOS MAKES NO REPRESENTATIONS AND WARRANTIES OF ANY KIND RELATED TO, BUT NOT LIMITED
 * TO THE NON-INFRINGEMENT OF THIRD PARTIES’ INTELLECTUAL PROPERTY RIGHTS OR THE
 * MERCHANTABILITY OR FITNESS FOR YOUR INTENDED PURPOSE OR USAGE. WÜRTH ELEKTRONIK EISOS DOES NOT
 * WARRANT OR REPRESENT THAT ANY LICENSE, EITHER EXPRESS OR IMPLIED, IS GRANTED UNDER ANY PATENT
 * RIGHT, COPYRIGHT, MASK WORK RIGHT, OR OTHER INTELLECTUAL PROPERTY RIGHT RELATING TO ANY
 * COMBINATION, MACHINE, OR PROCESS IN WHICH THE PRODUCT IS USED. INFORMATION PUBLISHED BY
 * WÜRTH ELEKTRONIK EISOS REGARDING THIRD-PARTY PRODUCTS OR SERVICES DOES NOT CONSTITUTE A LICENSE
 * FROM WÜRTH ELEKTRONIK EISOS TO USE SUCH PRODUCTS OR SERVICES OR A WARRANTY OR ENDORSEMENT
 * THEREOF
 *
 * THIS SOURCE CODE IS PROTECTED BY A LICENSE.
 * FOR MORE INFORMATION PLEASE CAREFULLY READ THE LICENSE AGREEMENT FILE (license_terms_wsen_sdk.pdf)
 * LOCATED IN THE ROOT DIRECTORY OF THIS DRIVER PACKAGE.
 *
 * COPYRIGHT (c) 2025 Würth Elektronik eiSos GmbH & Co. KG
 *
 ***************************************************************************************************
 */

/**
 * @file
 * @brief Header file for WSEN-PDMS example.
 *
 * Basic usage of the PDMS differential pressure sensor connected via I2C.
 */

#ifndef PDMS_I2C_H_INCLUDED
#define PDMS_I2C_H_INCLUDED

/* Includes */
#include "WeSensorsSDK.h"

/* Macros and Constants */
#define PDMS_I2C_ADDRESS (uint8_t)0x6C          /**< PDMS I2C address without CRC */
#define PDMS_I2C_ADDRESS_CRC (uint8_t)0x6D      /**< PDMS I2C address with CRC */
#define PDMS_I2C_READ_MEASUREMENT (uint8_t)0x2E /**< PDMS I2C read measurement command*/
#define CRC4_I2C_POLYNOMIAL (uint8_t)0x03       /**< CRC-4 polynomial used for I2C communication */
#define CRC4_I2C_INIT (uint8_t)0x0F             /**< Initial value for CRC-4 calculation */
#define CRC8_I2C_POLYNOMIAL (uint8_t)0xD5       /**< CRC-8 polynomial used for I2C communication */
#define CRC8_I2C_INIT (uint8_t)0xFF             /**< Initial value for CRC-8 calculation */

#define P_MIN_TYP_VAL_PDMS (uint16_t)3277  /**< Typical RAW value at minimum pressure, calibrated */
#define P_MAX_TYP_VAL_PDMS (uint16_t)29491 /**< Typical RAW value at maximum pressure, calibrated */
#define T_MIN_TYP_VAL_PDMS (uint16_t)8192  /**< Typical RAW value at minimum temperature in degrees Celsius = 0°C, calibrated */

/* PDMS Sensor types and their Specifications */
typedef enum
{
    PDMS_pdms0 = 0,       /**< Order code 2513130810105, range = -1 to + 1 kPa */
    PDMS_pdms1 = 1,       /**< Order code 2513130810205 range = -10 to + 10 kPa */
    PDMS_pdms2 = 2,       /**< Order code 2513130835205, range = -35 to + 35 kPa */
    PDMS_pdms3 = 3,       /**< Order code 2513130810305, range =  0 to 100 kPa */
    PDMS_pdms4 = 4,       /**< Order code 2513130810405  -100 to 1000 kPa  */
    PDMS_invalid = 0xFFFF /**< Invalid sensor type */
} PDMS_SensorType_t;

    /* Function definitions */
int8_t PDMS_getDefaultInterface(WE_sensorInterface_t* sensorInterface);
int8_t PDMS_I2C_GetRawPressureAndTemperature(WE_sensorInterface_t* sensorInterface, uint16_t* rawPressure, uint16_t* rawTemperature, uint16_t* syncStatusValue);
int8_t PDMS_I2C_GetRawPressureAndTemperature_WithCRC(WE_sensorInterface_t* sensorInterface, uint16_t* rawPressure, uint16_t* rawTemperature, uint16_t* syncStatusValue);
int8_t PDMS_getPressureAndTemperature_float(WE_sensorInterface_t* sensorInterface, PDMS_SensorType_t type, float* pressureKPa, float* temperatureDegC, uint16_t* syncStatusValue);

void WE_pdmsI2cExampleInit();
void WE_pdmsI2cExampleLoop();

void Get_pdms(float * presskPa, float * tempDegC );

#endif /* PDMS_I2C_H_INCLUDED */
