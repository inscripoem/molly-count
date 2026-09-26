/*****************************************************************************
* | File      	:   EPD_1in54g.c
* | Author     	:   Waveshare team
* | Function   	:   1inch54 e-paper (G)
* | Info       	:   copied from reference/waveshare/EPD_1in54g.cpp — the
*                 command sequences there are the source of truth.
*                 molly-count deltas (functionality ported from the Rust
*                 firmware, timings untouched):
*                   - ReadBusyH blinks EPD_LED_PIN (~4Hz) while the panel is
*                     busy and times out after 60s (vendor polls forever);
*                     Init/Display/Clear propagate 0 ok / -1 timeout.
*****************************************************************************/
#include "EPD_1in54g.h"

#define BUSY_TIMEOUT_MS 60000UL


/******************************************************************************
function :	Software reset
parameter:
******************************************************************************/
static void EPD_1IN54G_Reset(void)
{
    DEV_Digital_Write(EPD_RST_PIN, 1);
    DEV_Delay_ms(200);
    DEV_Digital_Write(EPD_RST_PIN, 0);
    DEV_Delay_ms(2);
    DEV_Digital_Write(EPD_RST_PIN, 1);
    DEV_Delay_ms(200);
}

/******************************************************************************
function :	send command
parameter:
     Reg : Command register
******************************************************************************/
static void EPD_1IN54G_SendCommand(UBYTE Reg)
{
    DEV_Digital_Write(EPD_DC_PIN, 0);
    DEV_Digital_Write(EPD_CS_PIN, 0);
    DEV_SPI_WriteByte(Reg);
    DEV_Digital_Write(EPD_CS_PIN, 1);
}

/******************************************************************************
function :	send data
parameter:
    Data : Write data
******************************************************************************/
static void EPD_1IN54G_SendData(UBYTE Data)
{
    DEV_Digital_Write(EPD_DC_PIN, 1);
    DEV_Digital_Write(EPD_CS_PIN, 0);
    DEV_SPI_WriteByte(Data);
    DEV_Digital_Write(EPD_CS_PIN, 1);
}

/******************************************************************************
function :	Wait until the busy_pin goes LOW
parameter:
******************************************************************************/
void EPD_1IN54G_ReadBusy(void)
{
    DEV_Delay_ms(100);
    while(DEV_Digital_Read(EPD_BUSY_PIN) == 1) {      //LOW: idle, HIGH: busy
        DEV_Delay_ms(100);
    }
}

/******************************************************************************
function :	Wait until the busy_pin goes HIGH (LOW: idle, HIGH: busy)
return   :	0 ok, -1 timeout
******************************************************************************/
static int EPD_1IN54G_ReadBusyH(void)
{
    DEV_Delay_ms(100);
    unsigned long start = millis();
    int polls = 0;
    extern bool g_stay_awake;    // main.cpp: park-after-cycle trigger
    extern unsigned long g_pwr_hold_ms; // cumulative PWR-pressed time (ms)
    while(!DEV_Digital_Read(EPD_BUSY_PIN)) {          //LOW: idle, HIGH: busy
        polls += 1;
        // ~4Hz blink while the panel works: the sealed-case busy signal.
        // Once the park gesture is armed the LED stops blinking entirely
        // (solid = parked-after-this-refresh).
        if (!g_stay_awake && polls % 25 == 0) {
            digitalWrite(EPD_LED_PIN, !digitalRead(EPD_LED_PIN));
        }
        // molly-count: PWR pressed time accumulates across the whole wake;
        // 5s total (single hold or split presses) parks the device awake
        // after this refresh — the console window alone was too easy to miss
        if (digitalRead(PWR_BTN_PIN) == LOW) { // active-LOW button
            g_pwr_hold_ms += 5;
            if (!g_stay_awake && g_pwr_hold_ms >= 5000) {
                g_stay_awake = true;
                digitalWrite(EPD_LED_PIN, LOW); // solid: armed
                Serial.print("stay: park armed (PWR, refresh phase)\r\n");
            }
        }
        if (millis() - start > BUSY_TIMEOUT_MS) {
            return -1;
        }
        DEV_Delay_ms(5);
    }
    return 0;
}

/******************************************************************************
function :	Turn On Display
parameter:
******************************************************************************/
static int EPD_1IN54G_TurnOnDisplay(void)
{
    EPD_1IN54G_SendCommand(0x12); // DISPLAY_REFRESH
    EPD_1IN54G_SendData(0x00);
    return EPD_1IN54G_ReadBusyH();
}

/******************************************************************************
function :	Initialize the e-paper register
parameter:
******************************************************************************/
int EPD_1IN54G_Init(void)
{
    EPD_1IN54G_Reset();

    EPD_1IN54G_SendCommand(0x4D);
    EPD_1IN54G_SendData(0x78);

    EPD_1IN54G_SendCommand(0x00);	//PSR
    EPD_1IN54G_SendData(0x0F);
    EPD_1IN54G_SendData(0x29);

    EPD_1IN54G_SendCommand(0X06); //BTST_P
    EPD_1IN54G_SendData(0x0D);
    EPD_1IN54G_SendData(0x12);
    EPD_1IN54G_SendData(0x30);
    EPD_1IN54G_SendData(0x20);
    EPD_1IN54G_SendData(0x19);
    EPD_1IN54G_SendData(0x2A);
    EPD_1IN54G_SendData(0x22);

    EPD_1IN54G_SendCommand(0x50);	//CDI
    EPD_1IN54G_SendData(0x37);

    EPD_1IN54G_SendCommand(0x61); //TRES
    EPD_1IN54G_SendData(EPD_1IN54G_WIDTH/256);
    EPD_1IN54G_SendData(EPD_1IN54G_WIDTH%256);
    EPD_1IN54G_SendData(EPD_1IN54G_HEIGHT/256);
    EPD_1IN54G_SendData(EPD_1IN54G_HEIGHT%256);

    EPD_1IN54G_SendCommand(0xE9);
    EPD_1IN54G_SendData(0x01);

    EPD_1IN54G_SendCommand(0x30);
    EPD_1IN54G_SendData(0x08);

    EPD_1IN54G_SendCommand(0x04);
    return EPD_1IN54G_ReadBusyH();
}

/******************************************************************************
function :	Initialize the e-paper register (fast refresh)
parameter:
******************************************************************************/
void EPD_1IN54G_Init_Fast(void)
{
    EPD_1IN54G_Reset();

    EPD_1IN54G_SendCommand(0x4D);
    EPD_1IN54G_SendData(0x78);

    EPD_1IN54G_SendCommand(0x00);	//PSR
    EPD_1IN54G_SendData(0x0F);
    EPD_1IN54G_SendData(0x29);

    EPD_1IN54G_SendCommand(0X06); //BTST_P
    EPD_1IN54G_SendData(0x0D);
    EPD_1IN54G_SendData(0x12);
    EPD_1IN54G_SendData(0x30);
    EPD_1IN54G_SendData(0x20);
    EPD_1IN54G_SendData(0x19);
    EPD_1IN54G_SendData(0x2A);
    EPD_1IN54G_SendData(0x22);

    EPD_1IN54G_SendCommand(0x50);	//CDI
    EPD_1IN54G_SendData(0x37);

    EPD_1IN54G_SendCommand(0x61); //TRES
    EPD_1IN54G_SendData(EPD_1IN54G_WIDTH/256);
    EPD_1IN54G_SendData(EPD_1IN54G_WIDTH%256);
    EPD_1IN54G_SendData(EPD_1IN54G_HEIGHT/256);
    EPD_1IN54G_SendData(EPD_1IN54G_HEIGHT%256);

    EPD_1IN54G_SendCommand(0xE9);
    EPD_1IN54G_SendData(0x01);

    EPD_1IN54G_SendCommand(0x30);
    EPD_1IN54G_SendData(0x08);

    EPD_1IN54G_SendCommand(0x04);
    EPD_1IN54G_ReadBusyH();

    EPD_1IN54G_SendCommand(0xE0);
    EPD_1IN54G_SendData(0x02);

    EPD_1IN54G_SendCommand(0xE6);
    EPD_1IN54G_SendData(0x5D);

    EPD_1IN54G_SendCommand(0xA5);
    EPD_1IN54G_SendData(0x00);
    EPD_1IN54G_ReadBusyH();
}

/******************************************************************************
function :	Clear screen
parameter:
******************************************************************************/
int EPD_1IN54G_Clear(UBYTE color)
{
    UWORD Width, Height;
    Width = (EPD_1IN54G_WIDTH % 4 == 0)? (EPD_1IN54G_WIDTH / 4 ): (EPD_1IN54G_WIDTH / 4 + 1);
    Height = EPD_1IN54G_HEIGHT;

    EPD_1IN54G_SendCommand(0x10);
    for (UWORD j = 0; j < Height; j++) {
        for (UWORD i = 0; i < Width; i++) {
            EPD_1IN54G_SendData((color << 6) | (color << 4) | (color << 2) | color);
        }
    }

    return EPD_1IN54G_TurnOnDisplay();
}

/******************************************************************************
function :	Sends the image buffer in RAM to e-Paper and displays
parameter:
******************************************************************************/
int EPD_1IN54G_Display(const UBYTE *Image)
{
    UWORD Width, Height;
    Width = (EPD_1IN54G_WIDTH % 4 == 0)? (EPD_1IN54G_WIDTH / 4 ): (EPD_1IN54G_WIDTH / 4 + 1);
    Height = EPD_1IN54G_HEIGHT;

    EPD_1IN54G_SendCommand(0x10);
    for (UWORD j = 0; j < Height; j++) {
        for (UWORD i = 0; i < Width; i++) {
            EPD_1IN54G_SendData(Image[i + j * Width]);
        }
    }

    return EPD_1IN54G_TurnOnDisplay();
}

/******************************************************************************
function :	Enter sleep mode
parameter:
******************************************************************************/
void EPD_1IN54G_Sleep(void)
{
    EPD_1IN54G_SendCommand(0x02); // POWER_OFF
    EPD_1IN54G_SendData(0X00);
    EPD_1IN54G_ReadBusyH();
    EPD_1IN54G_SendCommand(0x07); // DEEP_SLEEP
    EPD_1IN54G_SendData(0XA5);
}
