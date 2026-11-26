// SPDX-License-Identifier: MIT
#pragma once
#include <stdint.h>
#include "example_gpio.h"

typedef uint8_t UBYTE;
typedef uint16_t UWORD;
typedef uint32_t UDOUBLE;
#define HIGH 1
#define LOW 0
#define EPD_SCK_PIN GPIO_NUM_9
#define EPD_MOSI_PIN GPIO_NUM_46
#define EPD_CS_M_PIN GPIO_NUM_10
#define EPD_CS_S_PIN GPIO_NUM_3
#define EPD_RST_PIN GPIO_NUM_2
#define EPD_BUSY_PIN GPIO_NUM_12
#define EPD_DC_PIN GPIO_NUM_11

#define DEV_Digital_Write(pin, level) ESP_ERROR_CHECK(gpio_set_level(pin, level))
#define DEV_Digital_Read(pin) gpio_get_level(pin)
#define DEV_Delay_ms(ms) vTaskDelay(pdMS_TO_TICKS(ms))

void example_epaper_begin();
void DEV_SPI_WriteByte(UBYTE byte);
void DEV_SPI_Write_nByte(UBYTE *data, UDOUBLE length);
