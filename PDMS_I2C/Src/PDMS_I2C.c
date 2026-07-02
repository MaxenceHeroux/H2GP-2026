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
 * @brief WSEN-PDMS example.
 *
 * Basic usage of the PDMS differential pressure sensor connected via I2C.
 */
#include "gpio.h"
#include "i2c.h"
#include "PDMS_I2C.h"
#include <math.h>
#include <stdbool.h>
#include <stdlib.h>
#include "platform.h"
#include <stdio.h>
#include <string.h>

#include "main.h"
#include "log.h"


/* Sensor interface configuration */
static WE_sensorInterface_t pdms;

/* PDMS sensor type */
static PDMS_SensorType_t pdmsSensorType;

/* Sensor initialization function */
static bool PDMS_init(void);

/* Functions to print float values */
static void debugPrintPressure_float(float pressureKPa);
static void debugPrintTemperature_float(float temperature);

/**
 * @brief Example initialization.
 * Call this function after HAL initialization.
 */




/**
 * @brief Default sensor interface configuration.
 */
static WE_sensorInterface_t PDMSDefaultSensorInterface = {.sensorType = WE_PDMS,
                                                          .interfaceType = WE_i2c,
                                                          .options = {.i2c = {.address = PDMS_I2C_ADDRESS_CRC, .burstMode = 1, .protocol = WE_i2cProtocol_Raw, .useRegAddrMsbForMultiBytesRead = 1, .reserved = 0}, .readTimeout = 1000, .writeTimeout = 1000},
                                                          .handle = 0};

/**
 * @brief Read data from sensor.
 * @param[in] sensorInterface Pointer to sensor interface
 * @param[in] numBytesToRead Number of bytes to be read
 * @param[out] data Read buffer
 * @return Error Code
 */
static inline int8_t PDMS_ReadReg(WE_sensorInterface_t* sensorInterface, uint16_t numBytesToRead, uint8_t* data)
{
    /* 0xFF is used here as a place holder and it will not be used in the WE_ReadReg with WE_i2cProtocol_Raw and useRegAddrMsbForMultiBytesRead = 1; */
    return WE_ReadReg(sensorInterface, 0xFF, numBytesToRead, data);
}

/**
 * @brief Write data to sensor.
 * @param[in] sensorInterface Pointer to sensor interface.
 * @param[in] numBytesToRead Number of bytes to be read.
 * @param[in] data Write buffer.
 * @return Error Code.
 */
static inline int8_t PDMS_WriteReg(WE_sensorInterface_t* sensorInterface, uint8_t regAdr, uint16_t numBytesToWrite, uint8_t* data)
{
    /* 0xFF is used here as a place holder and it will not be used in the WE_WriteReg with WE_i2cProtocol_Raw and useRegAddrMsbForMultiBytesRead = 1; */
    return WE_WriteReg(sensorInterface, 0xFF, numBytesToWrite, data);
}

/**
 * @brief Returns the default sensor interface configuration.
 * @param[out] sensorInterface Sensor interface configuration (output parameter)
 * @return Error code
 */
int8_t PDMS_getDefaultInterface(WE_sensorInterface_t* sensorInterface)
{
    *sensorInterface = PDMSDefaultSensorInterface;
    return WE_SUCCESS;
}

/**
  Calculates CRC4 given a polynom, initialization value and data
  @param polynom The Polynom to use for CRC-calculation
  @param init The initial value
  @param data The data to calculate the CRC4 for
  @param length The length of the data in bytes
  @return Calculated CRC4
*/
static uint8_t calc_crc4(uint8_t polynom, uint8_t init, uint8_t* data, uint16_t length)
{
    uint8_t crc;
    uint16_t byteIndex;
    int8_t bitIndex;

    /* Initialize CRC with the initial value */
    crc = init;
    for (byteIndex = 0; byteIndex < length; byteIndex++)
    {
        for (bitIndex = 7; bitIndex >= 0; bitIndex--)
        {
            /* Process only the 4 least significant bits of the last byte */
            if ((byteIndex >= length - 1) && (bitIndex < 4))
            {
                break;
            }

            /* Compare the most significant bit of crc with the current bit of data */
            if (((crc >> 3) & 0x01) != ((data[byteIndex] >> bitIndex) & 0x01))
            {
                /* Shift crc left by 1 bit and XOR with polynomial */
                crc = (crc << 1) ^ polynom;
            }
            else
            {
                /* Shift crc to left */
                crc = crc << 1;
            }
            /* Ensure crc remains a 4-bit value */
            crc = crc & 0x0F;
        }
    }

    /* Return the final 4-bit CRC value */
    return crc & 0x0F;
}

/**
  Calculates CRC8 given a polynom, initialization value and data
  @param polynom The Polynom to use for CRC-calculation
  @param init The initial value
  @param data The data to calculate the CRC8 for
  @param length The length of the data in bytes
  @return Calculated CRC8
*/
static uint8_t calc_crc8(uint8_t polynom, uint8_t init, uint8_t* data, uint16_t length)
{
    uint8_t crc;
    uint16_t byteIndex;
    int8_t bitIndex;

    /* Initialize CRC with the initial value */
    crc = init;
    for (byteIndex = 0; byteIndex < length; byteIndex++)
    {
        for (bitIndex = 7; bitIndex >= 0; bitIndex--)
        {
            /* Compare the most significant bit of crc (bit 7) with the current bit of data[byteIndex] */
            if (((crc >> 7) & 0x01) != ((data[byteIndex] >> bitIndex) & 0x01))
            {
                /* Shift crc left by 1 bit and XOR with polynomial */
                crc = (crc << 1) ^ polynom;
            }
            else
            {
                /* Shift crc left */
                crc = crc << 1;
            }
        }
    }

    /* Return the final 8-bit CRC value  */
    return crc & 0xFF;
}

/**
 * @brief Read the pressure and temperature values
 * @param[in] sensorInterface Pointer to sensor interface
 * @param[in] type PDMS sensor type (i.e., pressure measurement range) for internal conversion of pressure
 * @param[out] pressureKPa Pointer to pressure value in kPa
 * @param[out] temperatureDegC Pointer to temperature value in degrees Celsius
 * @param[out] syncStatusValue Pointer to store the synchronized status value
 * @retval Error code
 */
int8_t PDMS_getPressureAndTemperature_float(WE_sensorInterface_t* sensorInterface, PDMS_SensorType_t type, float* pressureKPa, float* temperatureDegC, uint16_t* syncStatusValue)
{
    uint16_t rawPressure = 0;
    uint16_t rawTemperature = 0;
    uint16_t statusValue = 0;

    /* Check if sensor interface is initialized */
    if (sensorInterface == NULL)
    {
        return WE_FAIL;
    }

    switch (sensorInterface->interfaceType)
    {
        case WE_i2c:
        {
            switch (sensorInterface->options.i2c.address)
            {
                case PDMS_I2C_ADDRESS_CRC:
                    if (WE_FAIL == PDMS_I2C_GetRawPressureAndTemperature_WithCRC(sensorInterface, &rawPressure, &rawTemperature, &statusValue))
                    {
                        return WE_FAIL;
                    }
                    break;

                case PDMS_I2C_ADDRESS:
                    if (WE_FAIL == PDMS_I2C_GetRawPressureAndTemperature(sensorInterface, &rawPressure, &rawTemperature, &statusValue))
                    {
                        return WE_FAIL;
                    }
                    break;

                default:
                    /* Invalid I2C address for PDMS type pressure sensors */
                    return WE_FAIL;
            }
        }
        break;


    }

    *syncStatusValue = statusValue;

    /* Apply temperature offset to raw temperature and convert to °C. Please refer to the manual for more details */
    *temperatureDegC = (((float)(rawTemperature - T_MIN_TYP_VAL_PDMS) * 4.272f) / 1000);

    /* Apply pressure offset to raw pressure and convert to KPa. Please refer to the manual for more details */
    /* Conversion logic is based on the sensor type */
    float adjustedRawPressure = (float)((float)rawPressure - (float)P_MIN_TYP_VAL_PDMS);
    switch (type)
    {
        case PDMS_pdms0:
            *pressureKPa = ((adjustedRawPressure * 7.63f) / 100000) - 1.0f;
            break;

        case PDMS_pdms1:
            *pressureKPa = ((adjustedRawPressure * 7.63f) / 10000) - 10.0f;
            break;

        case PDMS_pdms2:
            *pressureKPa = ((adjustedRawPressure * 2.67f) / 1000) - 35.0f;
            break;

        case PDMS_pdms3:
            *pressureKPa = ((adjustedRawPressure * 3.81f) / 1000);
            break;

        case PDMS_pdms4:
            *pressureKPa = ((adjustedRawPressure * 4.19f) / 100) - 100.0f;
            break;

        default:
            /* Invalid PDMS sensor type */
            return WE_FAIL;
    }

    return WE_SUCCESS;
}

/**
 * @brief Read the raw pressure and temperature values via I2C interface
 * @param[in] sensorInterface Pointer to sensor interface
 * @param[out] rawPressure Pointer to store the raw pressure value
 * @param[out] rawTemperature Pointer to store the raw temperature value
 * @param[out] syncStatusValue Pointer to store the synchronized status value
 * @retval Error code
 */
int8_t PDMS_I2C_GetRawPressureAndTemperature(WE_sensorInterface_t* sensorInterface, uint16_t* rawPressure, uint16_t* rawTemperature, uint16_t* syncStatusValue)
{
    uint8_t writeBuffer8 = PDMS_I2C_READ_MEASUREMENT;
    int8_t status = WE_FAIL;
    uint8_t readbuffer8[6];

    /* Invalid I2C check address for measurement without CRC */
    if (sensorInterface->options.i2c.address != PDMS_I2C_ADDRESS)
    {
        return WE_FAIL;
    }

    status = PDMS_WriteReg(sensorInterface, 0xFF, 1, &writeBuffer8);
    if (status != WE_SUCCESS)
    {
        return WE_FAIL;
    }

    status = PDMS_ReadReg(sensorInterface, 6, &readbuffer8[0]);
    if (status != WE_SUCCESS)
    {
        return WE_FAIL;
    }

    *rawTemperature = ((uint16_t)readbuffer8[1] << 8) | (uint16_t)readbuffer8[0];
    *rawPressure = ((uint16_t)readbuffer8[3] << 8) | (uint16_t)readbuffer8[2];
    *syncStatusValue = ((uint16_t)readbuffer8[5] << 8) | (uint16_t)readbuffer8[4];

    return WE_SUCCESS;
}

/**
 * @brief Read the raw pressure and temperature values with CRC check via I2C interface
 * @param[in] sensorInterface Pointer to sensor interface
 * @param[out] rawPressure Pointer to store the raw pressure value
 * @param[out] rawTemperature Pointer to store the raw temperature value
 * @param[out] syncStatusValue Pointer to store the synchronized status value
 * @retval Error code
 */
int8_t PDMS_I2C_GetRawPressureAndTemperature_WithCRC(WE_sensorInterface_t* sensorInterface, uint16_t* rawPressure, uint16_t* rawTemperature, uint16_t* syncStatusValue)
{
    uint8_t writeBuffer8[2] = {0};
    int8_t status = WE_FAIL;
    uint8_t readbuffer8[7];
    uint8_t crc4;
    uint8_t crc8;
    uint8_t combinedData[10];

    /* Invalid I2C address check for measurement with CRC */
    if (sensorInterface->options.i2c.address != PDMS_I2C_ADDRESS_CRC)
    {
        return WE_FAIL;
    }

    /* Set the first BYTE to the I2C read measurement command */
    writeBuffer8[0] = PDMS_I2C_READ_MEASUREMENT;
    /* Set the upper 4 bits of the second byte to (number of data bytes to read (6) - 1) which is 5 */
    writeBuffer8[1] = (6 - 1) << 4;

    /* Compute and place CRC4 value into the least significant 4 bits of the second byte */
    crc4 = calc_crc4(CRC4_I2C_POLYNOMIAL, CRC4_I2C_INIT, &writeBuffer8[0], 2);
    writeBuffer8[1] |= (crc4 & 0x0F);

    status = PDMS_WriteReg(sensorInterface, 0xff, 2, &writeBuffer8[0]);
    if (status != WE_SUCCESS)
    {
        return WE_FAIL;
    }

    status = PDMS_ReadReg(sensorInterface, 7, &readbuffer8[0]);
    if (status != WE_SUCCESS)
    {
        return WE_FAIL;
    }

    /* Combine all data into a single buffer for CRC calculation. Please refer to the manual for more information on CRC calculation */
    combinedData[0] = PDMS_I2C_ADDRESS_CRC << 1;
    combinedData[1] = writeBuffer8[0];
    combinedData[2] = writeBuffer8[1];
    combinedData[3] = (PDMS_I2C_ADDRESS_CRC << 1) | 1;
    memcpy(&combinedData[4], &readbuffer8[0], 6);

    crc8 = calc_crc8(CRC8_I2C_POLYNOMIAL, CRC8_I2C_INIT, &combinedData[0], 10);
    if (crc8 != readbuffer8[6])
    {
        return WE_FAIL;
    }

    *rawTemperature = ((uint16_t)readbuffer8[1] << 8) | (uint16_t)readbuffer8[0];
    *rawPressure = ((uint16_t)readbuffer8[3] << 8) | (uint16_t)readbuffer8[2];
    *syncStatusValue = ((uint16_t)readbuffer8[5] << 8) | (uint16_t)readbuffer8[4];

    return WE_SUCCESS;
}

void WE_pdmsI2cInit()
{
	/*
    char bufferMajor[4];
    char bufferMinor[4];
    sprintf(bufferMajor, "%d", WE_SENSOR_SDK_MAJOR_VERSION);
    sprintf(bufferMinor, "%d", WE_SENSOR_SDK_MINOR_VERSION);
    debugPrint("Wuerth Elektronik eiSos Sensors SDK version ");
    debugPrint(bufferMajor);
    debugPrint(".");
    debugPrintln(bufferMinor);
    debugPrintln("Pin CS/SA0 at power on connected to GND via pull-down resistors activates I2C communication with address 0x6C.");
    debugPrintln("This example gives I2C measurement with CRC activated.");
    debugPrintln("Select the i2c address PDMS_I2C_ADDRESS in PDMS_init() function for measurement without CRC.");
    debugPrintln("Select the right pdms sensor type in PDMS_init() function. PDMS_pdus3 is selected as default.");
*/
    /* init PDMS */
    if (false == PDMS_init()) LOG_ERROR("PDMS_Init() error. STOP");

}

/**
 * @brief Example main loop code.
 * Call this function in main loop (infinite loop).
 */
void WE_pdmsI2cExampleLoop()
{

    float presskPa;
    float tempDegC;
    uint16_t syncStatusValue;

    /* Please select PDMS_SensorType_t here accordingly to convert raw values to pressure values */
    if (WE_SUCCESS == PDMS_getPressureAndTemperature_float(&pdms, pdmsSensorType, &presskPa, &tempDegC, &syncStatusValue))
    {
        debugPrintPressure_float(presskPa);
        debugPrintTemperature_float(tempDegC);
        /* Enable the below code to print the synchronized status value */

		char syncStatusValueStr[8];
		sprintf(syncStatusValueStr, "0x%04X", syncStatusValue);
		debugPrint("Status value = ");
		debugPrintln(syncStatusValueStr);

    }
    else
    {
        debugPrintln("**** PDMS_getPressureAndTemperature_float(): Failed ****");
    }

    /* Delay of 1 second between successive measurements. */
    WE_Delay(1000);
}

/**
 * @brief Initializes the sensor for this example application.
 */
static bool PDMS_init(void)
{
    /* I2C communication shall be used with 100 kHz(Standard Mode)frequency. SPI is to be used for higher speed*/
    /* Initialize sensor interface (i2c with PDMS address, burst mode activated) */
    PDMS_getDefaultInterface(&pdms);
    pdms.interfaceType = WE_i2c;
    pdms.options.i2c.burstMode = 1;
    pdms.handle = &hi2c1;

    /* PDMS I2C Address Settings:
	   - PDMS_I2C_ADDRESS: PDMS I2C address without CRC (0x6C)
	   - PDMS_I2C_ADDRESS_CRC: PDMS I2C address with CRC (0x6D)
	*/
    pdms.options.i2c.address = PDMS_I2C_ADDRESS;

    /* PDMS Sensor Types and Their Specifications:
	   - PDMS_pdms0: Order code 2513130810105, range = -1 to +1 kPa
	   - PDMS_pdms1: Order code 2513130810205, range = -10 to +10 kPa
	   - PDMS_pdms2: Order code 2513130835205, range = -35 to +35 kPa
	   - PDMS_pdms3: Order code 2513130810305, range =  0 to 100 kPa
	   - PDMS_pdms4: Order code 2513130810405, range = -100 to 1000 kPa
	*/
    pdmsSensorType = PDMS_pdms3;

    /* Wait for boot */
    WE_Delay(50);
    int i = 0;
    while (WE_SUCCESS != WE_isSensorInterfaceReady(&pdms) && i<4)
    {
    	i++;
    	WE_Delay(10);
    }
    if( i > 2 ) {
    	return false;
    }

    return true;
}

/**
 * @brief Prints the pressure to the debug interface.
 * @param pressureKPa  Pressure [kPa]
 */
static void debugPrintPressure_float(float pressureKPa)
{
    float pressureAbs = fabs(pressureKPa);
    uint16_t full = (uint16_t)pressureAbs;
    uint16_t decimals = (uint16_t)(((uint32_t)(pressureAbs * 10000)) % 10000); /* 4 decimal places */

    char bufferFull[6];     /* Max 5 digits + null terminator */
    char bufferDecimals[5]; /* 4 decimal places + null terminator */
    sprintf(bufferFull, "%u", full);
    sprintf(bufferDecimals, "%04u", decimals);

    debugPrint("PDMS pressure (float) = ");
    if (pressureKPa < 0)
    {
        debugPrint("-");
    }
    debugPrint(bufferFull);
    debugPrint(".");
    debugPrint(bufferDecimals);
    debugPrintln(" kPa");
}

/**
 * @brief Prints the temperature to the debug interface.
 * @param tempDegC Temperature [°C]
 */
static void debugPrintTemperature_float(float tempDegC)
{
    float tempAbs = fabs(tempDegC);
    uint16_t full = (uint16_t)tempAbs;
    uint16_t decimals = ((uint16_t)(tempAbs * 100)) % 100; /* 2 decimal places */

    char bufferFull[6];     /* Max 5 digits + null terminator */
    char bufferDecimals[3]; /* 4 decimal places + null terminator */
    sprintf(bufferFull, "%u", full);
    sprintf(bufferDecimals, "%02u", decimals);

    debugPrint("PDMS temperature (float) = ");
    if (tempDegC < 0)
    {
        debugPrint("-");
    }
    debugPrint(bufferFull);
    debugPrint(".");
    debugPrint(bufferDecimals);
    debugPrintln(" degrees Celsius");
}


//home made
void Get_pdms(float * presskPa, float * tempDegC )
{
    uint16_t syncStatusValue;

    /* Please select PDMS_SensorType_t here accordingly to convert raw values to pressure values */
    if (WE_SUCCESS != PDMS_getPressureAndTemperature_float(&pdms, pdmsSensorType, presskPa, tempDegC, &syncStatusValue)) {
    	 LOG_WARN("Get_pdms(): Failed");
    }
}
