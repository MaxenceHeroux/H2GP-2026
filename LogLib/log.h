/*
 * log.h
 *
 *  Created on: 24 juin 2026
 *      Author: herou
 */

#ifndef LOG_H
#define LOG_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

void LOG_Init(void);
void LOG_Enable(uint8_t enable);
uint8_t LOG_IsEnabled(void);
void LOG_Send(const char *buf, uint16_t len);

#include <stdio.h>
#include <stm32l4xx_hal.h>

#define LOG_BUF_SIZE 128

#define LOG(fmt, ...) do { \
    char buf[LOG_BUF_SIZE]; \
    int len = snprintf(buf, sizeof(buf), fmt "\r\n", ##__VA_ARGS__); \
    LOG_Send(buf, len); \
} while(0)

#define LOG_INFO(fmt, ...) do { \
    char buf[LOG_BUF_SIZE]; \
    int len = snprintf(buf, sizeof(buf), "[%lu][INFO] " fmt "\r\n", HAL_GetTick(), ##__VA_ARGS__); \
    LOG_Send(buf, len); \
} while(0)

#define LOG_WARN(fmt, ...) do { \
    char buf[LOG_BUF_SIZE]; \
    int len = snprintf(buf, sizeof(buf), "[WARN] " fmt "\r\n", ##__VA_ARGS__); \
    LOG_Send(buf, len); \
    RGB_Set(255,80,0);\
    HAL_Delay(200); \
    RGB_Set(0,0,0);\
} while(0)

#define LOG_ERROR(fmt, ...) do { \
    char buf[LOG_BUF_SIZE]; \
    int len = snprintf(buf, sizeof(buf), "[ERROR] " fmt "\r\n", ##__VA_ARGS__); \
    LOG_Send(buf, len); \
    RGB_Set(255,0,0);\
    HAL_Delay(200); \
    RGB_Set(0,0,0);\
} while(0)

#ifdef __cplusplus
}
#endif

#endif
