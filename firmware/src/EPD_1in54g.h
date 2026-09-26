/*****************************************************************************
* | File      	:   EPD_1in54g.h
* | Author     	:   Waveshare team
* | Function   	:   1inch54 e-paper (G)
* | Info       	:   copied from reference/waveshare/EPD_1in54g.h; Init /
*                 Display / Clear return int (0 ok, -1 busy timeout) —
*                 molly-count: the vendor code polls forever.
*****************************************************************************/
#ifndef __EPD_1IN54G_H_
#define __EPD_1IN54G_H_

#include "DEV_Config.h"

// Display resolution
#define EPD_1IN54G_WIDTH       200
#define EPD_1IN54G_HEIGHT      200

// Color
#define  EPD_1IN54G_BLACK   0x0
#define  EPD_1IN54G_WHITE   0x1
#define  EPD_1IN54G_YELLOW  0x2
#define  EPD_1IN54G_RED     0x3

int EPD_1IN54G_Init(void);
void EPD_1IN54G_Init_Fast(void);
int EPD_1IN54G_Clear(UBYTE color);
int EPD_1IN54G_Display(const UBYTE *Image);
void EPD_1IN54G_Sleep(void);

#endif
