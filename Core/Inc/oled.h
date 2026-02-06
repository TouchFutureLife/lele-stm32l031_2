#ifndef __SSD1315_H
#define __SSD1315_H

#include "main.h"

/********************* Hardware Configuration *********************/
// I2C Address: SA0=GND→0x78, SA0=VCC→0x7A
#define SSD1315_I2C_ADDR    0x78
// Screen Resolution (60×32) per panel datasheet
#define SSD1315_WIDTH       60      // Column (0~59)
#define SSD1315_COL_OFFSET   34     // Fine-tune visible window start to reduce leading artifact
#define SSD1315_HEIGHT      32      // Row (0~31)
#define SSD1315_PAGE_NUM    4       // Page (32 rows/8 = 4 pages)

// Reset Pin Configuration (use CubeMX generated macro)
#define SSD1315_USE_RST_PIN 1       // 1=Enable reset, 0=Disable
#if SSD1315_USE_RST_PIN
#define SSD1315_RST_PORT    LCD_RST_GPIO_Port   // CubeMX GPIO Port
#define SSD1315_RST_PIN     LCD_RST_Pin         // CubeMX GPIO Pin
#endif

// Debug Configuration (enable printf log via UART)
#define SSD1315_DEBUG       0
#if SSD1315_DEBUG
#include <stdio.h>
#define SSD1315_LOG(fmt, ...)  printf("[SSD1315] " fmt "\r\n", ##__VA_ARGS__)
#else
#define SSD1315_LOG(...)
#endif

// Display Direction Configuration (adjust for garbled/mirror)
#define SSD1315_SEG_REMAP       0xA0    // 0xA0=Normal, 0xA1=Column mirror
#define SSD1315_COM_SCAN        0xC0    // 0xC0=Normal scan, 0xC8=Reverse scan
#define SSD1315_CONTRAST        0xCF    // Standard contrast value

// I2C Handle (I2C1 must be initialized in main.c)
extern I2C_HandleTypeDef hi2c1;
#define SSD1315_I2C_HANDLE  hi2c1

/********************* Status Enum *********************/
typedef enum {
    SSD1315_OK   = 0,  // Success
    SSD1315_ERR  = 1   // Failed
} SSD1315_StatusTypeDef;

/********************* GRAM Declaration *********************/
// GRAM Format: [Page][Column], 4 pages × 60 columns
extern uint8_t OLED_GRAM[SSD1315_PAGE_NUM][SSD1315_WIDTH];

/********************* Function Declaration *********************/
// Initialize SSD1315 (return status)
SSD1315_StatusTypeDef SSD1315_Init(void);
// Clear screen (GRAM = 0)
void SSD1315_Clear(void);
// Refresh GRAM to screen
void SSD1315_Refresh_Gram(void);
// Draw pixel (x:0~59, y:0~31, color:0=black/1=white)
void SSD1315_DrawPoint(uint8_t x, uint8_t y, uint8_t color);
// Display larger text (8x16 glyphs for digits/.,space,°/C)
void SSD1315_ShowBigText(uint8_t x, uint8_t y, const char *str, uint8_t color);
// Fill entire screen (color:0=black/1=white)
void SSD1315_FillScreen(uint8_t color);
// Fill single page (page:0~3, color:0=black/1=white)
void SSD1315_FillPage(uint8_t page, uint8_t color);

#endif
