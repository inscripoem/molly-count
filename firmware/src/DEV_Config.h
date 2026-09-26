/*****************************************************************************
* | File      	:   DEV_Config.h
* | Author     	:   Waveshare team
* | Function   	:   Hardware underlying interface
* | Info       	:   copied from reference/waveshare/DEV_Config.h (this exact
*                 board); deltas marked "molly-count".
*****************************************************************************/
#ifndef _DEV_CONFIG_H_
#define _DEV_CONFIG_H_

#include <Arduino.h>
#include <stdint.h>
#include <stdio.h>

/**
 * data
**/
#define UBYTE   uint8_t
#define UWORD   uint16_t
#define UDOUBLE uint32_t

/**
 * GPIO config
**/
#define EPD_SCK_PIN  12
#define EPD_MOSI_PIN 13
#define EPD_CS_PIN   11
#define EPD_DC_PIN   10
#define EPD_RST_PIN  9
#define EPD_BUSY_PIN 8

// molly-count: EPD power rail (GPIO6, active-low) is owned by main.cpp, so
// the vendor D_9PIN block stays off. Vendor DEV_Module_Exit would drive the
// rail LOW, which per the schematic turns the rail ON — not what "exit"
// should do on this board.
#define D_9PIN  0

// molly-count: green LED (low-active), blinked while the panel reports busy
// (sealed-case "panel is working" signal).
#define EPD_LED_PIN 3

// molly-count: PWR button (BAT_KEY, active-HIGH via VBAT+diode on press).
#define PWR_BTN_PIN 18

#define GPIO_PIN_SET   1
#define GPIO_PIN_RESET 0

/**
 * GPIO read and write
**/
#define DEV_Digital_Write(_pin, _value) digitalWrite(_pin, _value == 0? LOW:HIGH)
#define DEV_Digital_Read(_pin) digitalRead(_pin)

/**
 * delay x ms
**/
#define DEV_Delay_ms(__xms) delay(__xms)

/*------------------------------------------------------------------------------------------------------*/
UBYTE DEV_Module_Init(void);
void GPIO_Mode(UWORD GPIO_Pin, UWORD Mode);
void DEV_SPI_WriteByte(UBYTE data);
UBYTE DEV_SPI_ReadByte();
void DEV_SPI_Write_nByte(UBYTE *pData, UDOUBLE len);
void DEV_Module_Exit(void);

#endif
