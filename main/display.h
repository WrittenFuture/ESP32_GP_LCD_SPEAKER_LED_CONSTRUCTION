#include "util.h"

// LCD pins
#define LCD_RST 16
#define LCD_CS 17
#define LCD_RS 22
#define LCD_WR 2
//#define LCD_RD 22

#define LCD_D0 12
#define LCD_D1 13
#define LCD_D2 32
#define LCD_D3 33
#define LCD_D4 25
#define LCD_D5 26
#define LCD_D6 27
#define LCD_D7 14

#define LCD_WIDTH 480
#define LCD_HEIGHT 320
#define BUFFER_LINES 8
#define DISPLAY_BUFFER_SIZE (LCD_WIDTH * BUFFER_LINES * 2)

static esp_lcd_i80_bus_handle_t lcd_bus;
static esp_lcd_panel_io_handle_t lcd_io;

typedef struct
{
    uint8_t r;
    uint8_t g;
    uint8_t b;
} rgb_t;

static const rgb_t colors[] = {
    {.r = 255, .g = 255, .b = 255}, // White
    {.r = 0,   .g = 0,   .b = 255}, // Blue
    {.r = 0,   .g = 255, .b = 0},   // Green
    {.r = 255, .g = 0,   .b = 0},   // Red
    {.r = 0,   .g = 0,   .b = 0},   // Black
};
// functions


buffer_u8_t display_init(void);
void display_flush(uint16_t color, buffer_u8_t display_buff);
void display_off(void);
void display_on(void);
/*
here are some good commands to know
0x01 	SOFTRESET 	0 	Software reset; wait ≥5 ms after
0x11 	SLPOUT 	0 	Exit sleep; wait ≥120 ms
0x20 	INVOFF 	0 	Disable inversion
0x28 	DISPOFF 	0 	Blank display
0x29 	DISPON 	0 	Turn display output on
0x2A 	CASET 	4 	Column address set: SC[15:8], SC[7:0], EC[15:8], EC[7:0]
0x2B 	RASET 	4 	Row address set: SP[15:8], SP[7:0], EP[15:8], EP[7:0]
0x2C 	RAMWR 	n × 2 	Memory write; pixel stream, MSB first, RGB565
0x36 	MADCTL 	1 	Memory access control: rotation, BGR/RGB, h/v mirror
0x3A 	COLMOD 	1 	Pixel format: 0x55 = RGB565
0xB1 	FRMCTR1 	2 	Frame rate control (normal mode)
0xB6 	DFUNCTR 	3 	Display function control
0xB7 	ENTRYMODE 	1 	Entry mode set (tear, etc.)
0xC0 	PWCTR1 	1 	Power control 1
0xC5 	VMCTR1 	1 	VCOM control 1
0xD0 	PWCTR3 	1 	Power control 3 (partial mode)
0xE0 	PVGAMCTRL 	15 	Positive gamma correction
0xE1 	NVGAMCTRL 	15 	Negative gamma correction
*/