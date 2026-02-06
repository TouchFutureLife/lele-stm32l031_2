#include "oled.h"
#include "i2c.h"
#include "gpio.h"
#include <string.h>

/********************* 全局变量 *********************/
uint8_t OLED_GRAM[SSD1315_PAGE_NUM][SSD1315_WIDTH] = { 0 };


// 8x16 font for digits / '.' / space / 'C' / degree symbol
// Stored row-major, MSB = leftmost pixel
static const uint8_t FONT8x16[][16] = {
    /* '0' */ {0x00,0x3C,0x42,0x81,0x81,0x81,0x81,0x81,0x81,0x81,0x81,0x81,0x42,0x3C,0x00,0x00},
    /* '1' (thin with wider base) */ {0x00,0x08,0x18,0x28,0x08,0x08,0x08,0x08,0x08,0x08,0x08,0x08,0x3C,0x00,0x00,0x00},
    /* '2' */ {0x00,0x3C,0x42,0x81,0x01,0x01,0x02,0x04,0x08,0x10,0x20,0x40,0x81,0xFF,0x00,0x00},
    /* '3' */ {0x00,0x7E,0x01,0x01,0x01,0x01,0x1E,0x01,0x01,0x01,0x01,0x01,0x01,0x7E,0x00,0x00},
    /* '4' */ {0x00,0x02,0x06,0x0A,0x12,0x22,0x42,0x82,0xFF,0x02,0x02,0x02,0x02,0x02,0x00,0x00},
    /* '5' */ {0x00,0xFF,0x80,0x80,0x80,0x80,0xFC,0x02,0x01,0x01,0x01,0x01,0x02,0xFC,0x00,0x00},
    /* '6' */ {0x00,0x3E,0x40,0x80,0x80,0x80,0xFC,0x82,0x81,0x81,0x81,0x81,0x42,0x3C,0x00,0x00},
    /* '7' */ {0x00,0xFF,0x01,0x02,0x04,0x08,0x10,0x10,0x20,0x20,0x40,0x40,0x40,0x40,0x00,0x00},
    /* '8' */ {0x00,0x3C,0x42,0x81,0x81,0x42,0x3C,0x42,0x81,0x81,0x81,0x81,0x42,0x3C,0x00,0x00},
    /* '9' */ {0x00,0x3C,0x42,0x81,0x81,0x81,0x43,0x3D,0x01,0x01,0x01,0x02,0x04,0xF8,0x00,0x00},
    /* '.' (enlarge to 4x4 block) */ {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x30,0x30,0x30,0x30,0x00,0x00},
    /* ' ' */ {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    /* 'C' */ {0x00,0x3E,0x41,0x80,0x80,0x80,0x80,0x80,0x80,0x80,0x80,0x80,0x41,0x3E,0x00,0x00},
    /* '°' (larger) */ {0x00,0x1C,0x22,0x41,0x41,0x22,0x1C,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}
};

/********************* 私有函数 *********************/
static void SSD1315_WriteCmd(uint8_t cmd)
{
    HAL_StatusTypeDef status;
    uint8_t retry = 3;
    
    while(retry--) {
        // 使用Master_Transmit传输命令（第一个字节是命令模式标志0x00）
        uint8_t buf[2] = {0x00, cmd};
        status = HAL_I2C_Master_Transmit(&SSD1315_I2C_HANDLE, SSD1315_I2C_ADDR, buf, 2, 100);
        if(status == HAL_OK) return;
        HAL_Delay(1);
    }
}

static void SSD1315_WriteData(uint8_t* data, uint16_t len)
{
    HAL_StatusTypeDef status;
    uint8_t retry = 3;
    
    // 使用栈上的静态缓冲区，避免动态内存分配
    static uint8_t buf[SSD1315_WIDTH + 1];
    
    // 安全检查：确保数据长度不超过缓冲区大小
    if(len > SSD1315_WIDTH) return;
    
    buf[0] = 0x40;  // 0x40 = 数据模式标志
    memcpy(buf + 1, data, len);
    
    while(retry--) {
        // 使用Master_Transmit传输数据（第一个字节是控制字节0x40）
        status = HAL_I2C_Master_Transmit(&SSD1315_I2C_HANDLE, SSD1315_I2C_ADDR, buf, len + 1, 100);
        if(status == HAL_OK) return;
        HAL_Delay(1);
    }
}

#if SSD1315_USE_RST_PIN
static void SSD1315_Reset(void)
{
    SSD1315_RST_PORT->ODR &= ~SSD1315_RST_PIN; // 拉低RST
    HAL_Delay(100); // 延时
    SSD1315_RST_PORT->ODR |= SSD1315_RST_PIN;  // 拉高RST
    HAL_Delay(100); // 延时
}
#endif

/********************* 公有函数 *********************/
SSD1315_StatusTypeDef SSD1315_Init(void)
{
#if SSD1315_USE_RST_PIN
    SSD1315_Reset();
#endif
    
    HAL_Delay(100); // 上电延时
    
    // 完全按照商家提供的初始化序列
    SSD1315_WriteCmd(0xae); /* display off */ 
    SSD1315_WriteCmd(0x20); /*Set Memory Addressing Mode*/
    SSD1315_WriteCmd(0x02); /*Page addressing mode*/
    SSD1315_WriteCmd(0x00 | (SSD1315_COL_OFFSET & 0x0F)); /*set lower column address with offset*/ 
    SSD1315_WriteCmd(0x10 | ((SSD1315_COL_OFFSET >> 4) & 0x0F)); /*set higher column address with offset*/ 
    SSD1315_WriteCmd(0x00); /*set display start line*/ 
    SSD1315_WriteCmd(0xB0); /*set page address*/ 
    SSD1315_WriteCmd(0x81); /*contract control*/ 
    SSD1315_WriteCmd(0x18); 
    SSD1315_WriteCmd(SSD1315_SEG_REMAP); /*set segment remap*/ 
    SSD1315_WriteCmd(0xA6); /*normal / reverse*/ 
    SSD1315_WriteCmd(0xA8); /*multiplex ratio*/ 
    SSD1315_WriteCmd(0x1F); /*duty = 1/32*/ 
    SSD1315_WriteCmd(SSD1315_COM_SCAN); /*Com scan direction*/ 
    SSD1315_WriteCmd(0xD3); /*set display offset*/ 
    SSD1315_WriteCmd(0x00); 
    SSD1315_WriteCmd(0xD5); /*set osc division*/ 
    SSD1315_WriteCmd(0x80); 
    SSD1315_WriteCmd(0xD9); /*set pre-charge period*/ 
    SSD1315_WriteCmd(0xf1); 
    SSD1315_WriteCmd(0xDA); /*set COM pins*/ 
    SSD1315_WriteCmd(0x12); 
    SSD1315_WriteCmd(0xdb); /*set vcomh*/ 
    SSD1315_WriteCmd(0x40); 
    SSD1315_WriteCmd(0x8d); /*set charge pump enable*/ 
    SSD1315_WriteCmd(0x14);                       
    SSD1315_WriteCmd(0xAD); /*Internal IREF Setting*/ 
    SSD1315_WriteCmd(0x30); 
    SSD1315_WriteCmd(0xAF); /*display ON*/
    
    HAL_Delay(100); // 延时等待稳定
    
    SSD1315_Clear();
    SSD1315_Refresh_Gram();
    
    return SSD1315_OK;
}

void SSD1315_Clear(void)
{
    memset(OLED_GRAM, 0, sizeof(OLED_GRAM));
}

void SSD1315_Refresh_Gram(void)
{
    uint8_t page;
    
    for(page = 0; page < SSD1315_PAGE_NUM; page++) {
        SSD1315_WriteCmd(0xB0 + page); // 设置页地址

        uint8_t col = 0;
        while(col < SSD1315_WIDTH) {
            uint8_t chunk = SSD1315_WIDTH - col;
            if(chunk > 32) chunk = 32; // 分块避免过长的I2C传输

            uint8_t hw_col = col + SSD1315_COL_OFFSET;

            // 设置当前列地址（带偏移）
            SSD1315_WriteCmd(0x00 | (hw_col & 0x0F));
            SSD1315_WriteCmd(0x10 | ((hw_col >> 4) & 0x0F));

            SSD1315_WriteData(&OLED_GRAM[page][col], chunk);
            col += chunk;
        }
    }
}

void SSD1315_DrawPoint(uint8_t x, uint8_t y, uint8_t color)
{
    if(x >= SSD1315_WIDTH || y >= SSD1315_HEIGHT) return;
    
    uint8_t page = y >> 3; // y / 8
    uint8_t bit = y & 7;   // y % 8
    
    if(color) {
        OLED_GRAM[page][x] |= (1 << bit);
    } else {
        OLED_GRAM[page][x] &= ~(1 << bit);
    }
}

// Map character to FONT8x16 index; returns 0xFF if unsupported
static uint8_t SSD1315_MapBigChar(char ch)
{
    uint8_t ch8 = (uint8_t)ch;
    if(ch >= '0' && ch <= '9') return (uint8_t)(ch - '0');
    if(ch == '.') return 10;
    if(ch == ' ') return 11;
    if(ch == 'C' || ch == 'c') return 12;
    // accept 'o'/'O' or 0xB0 (common code for '°') as degree
    if(ch == 'o' || ch == 'O' || ch8 == 0xB0) return 13;
    return 0xFF;
}

// Draw one 8x16 glyph at (x,y)
static void SSD1315_DrawBigChar(uint8_t x, uint8_t y, const uint8_t font16[16], uint8_t color)
{
    for(uint8_t row = 0; row < 16; row++) {
        uint8_t bits = font16[row];
        for(uint8_t col = 0; col < 8; col++) {
            uint8_t pixel = (bits >> (7 - col)) & 0x01;
            SSD1315_DrawPoint(x + col, y + row, color ? pixel : !pixel);
        }
    }
}

// Display larger text using 8x16 digits/dot/space/°/C
void SSD1315_ShowBigText(uint8_t x, uint8_t y, const char *str, uint8_t color)
{
    while(*str) {
        uint8_t idx = SSD1315_MapBigChar(*str);
        uint8_t advance;
        if(idx == 0xFF) {
            advance = 4; // skip unsupported chars with small gap
        } else {
            SSD1315_DrawBigChar(x, y, FONT8x16[idx], color);
            // widen spacing for digits/C; keep punctuation tighter
            if(idx == 10) {
                advance = 7; // dot
            } else if(idx == 11) {
                advance = 6; // space
            } else if(idx == 13) {
                advance = 8; // degree
            } else {
                advance = 10; // digits + 'C'
            }
        }
        x += advance;
        str++;
    }
}

void SSD1315_FillScreen(uint8_t color)
{
    uint8_t page, col;
    uint8_t fill_data = color ? 0xFF : 0x00;
    
    for(page = 0; page < SSD1315_PAGE_NUM; page++) {
        for(col = 0; col < SSD1315_WIDTH; col++) {
            OLED_GRAM[page][col] = fill_data;
        }
    }
    SSD1315_Refresh_Gram();
}

void SSD1315_FillPage(uint8_t page, uint8_t color)
{
    uint8_t col;
    uint8_t fill_data = color ? 0xFF : 0x00;
    
    if(page >= SSD1315_PAGE_NUM) return;
    
    for(col = 0; col < SSD1315_WIDTH; col++) {
        OLED_GRAM[page][col] = fill_data;
    }
    SSD1315_Refresh_Gram();
}

void SSD1315_DrawLine(uint8_t x1, uint8_t y1, uint8_t x2, uint8_t y2, uint8_t color)
{
    int16_t dx = x2 - x1;
    int16_t dy = y2 - y1;
    int16_t sx = dx > 0 ? 1 : -1;
    int16_t sy = dy > 0 ? 1 : -1;
    
    dx = dx > 0 ? dx : -dx;
    dy = dy > 0 ? dy : -dy;
    
    int16_t err = dx - dy;
    int16_t e2;
    
    int16_t x = x1;
    int16_t y = y1;
    
    while(1) {
        SSD1315_DrawPoint(x, y, color);
        
        if(x == x2 && y == y2) break;
        
        e2 = 2 * err;
        if(e2 > -dy) {
            err -= dy;
            x += sx;
        }
        if(e2 < dx) {
            err += dx;
            y += sy;
        }
    }
}

void SSD1315_DrawRect(uint8_t x1, uint8_t y1, uint8_t x2, uint8_t y2, uint8_t color)
{
    SSD1315_DrawLine(x1, y1, x2, y1, color);
    SSD1315_DrawLine(x1, y1, x1, y2, color);
    SSD1315_DrawLine(x2, y1, x2, y2, color);
    SSD1315_DrawLine(x1, y2, x2, y2, color);
}

void SSD1315_FillRect(uint8_t x1, uint8_t y1, uint8_t x2, uint8_t y2, uint8_t color)
{
    if(x1 > x2) {
        uint8_t temp = x1;
        x1 = x2;
        x2 = temp;
    }
    if(y1 > y2) {
        uint8_t temp = y1;
        y1 = y2;
        y2 = temp;
    }
    
    uint8_t x, y;
    for(y = y1; y <= y2; y++) {
        for(x = x1; x <= x2; x++) {
            SSD1315_DrawPoint(x, y, color);
        }
    }
    SSD1315_Refresh_Gram();
}
