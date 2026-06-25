/*
 * log.c
 *
 *  Created on: 24 juin 2026
 *      Author: heroux
 */


/* usb_log.c */

#include "log.h"
#include "usbd_cdc_if.h"
#include "usbd_core.h"
#include "main.h"

extern USBD_HandleTypeDef hUsbDeviceFS;

static uint8_t log_enabled = 1;

void LOG_Enable(uint8_t enable){
    log_enabled = enable;
}

uint8_t LOG_IsEnabled(void){
    return log_enabled && (hUsbDeviceFS.dev_state == USBD_STATE_CONFIGURED);
}

void LOG_Send(const char *buf, uint16_t len){
    if (!LOG_IsEnabled()) return;

    uint32_t start = HAL_GetTick();

    while (CDC_Transmit_FS((uint8_t*)buf, len) == USBD_BUSY){
        if ((HAL_GetTick() - start) > 50) return;
    }
}
