#ifndef __OLED_H__
#define __OLED_H__

#include "main.h"

#ifdef __cplusplus
extern "C" {
#endif

/********************* 硬件配置 *********************/
// I2C地址（SA0引脚接GND为0x78，接VCC为0x7A）
#define SSD1315_I2C_ADDR    0x78
// 屏幕分辨率
#define SSD1315_WIDTH       60  // 列数
#define SSD1315_HEIGHT      32  // 行数
#define SSD1315_PAGE_NUM    4   // 页数（32行/8行每页=4页）

// 字符显示参数（6×8字模）
#define CHAR_WIDTH          6   // 字符宽度
#define CHAR_HEIGHT         8   // 字符高度

/********************* 状态定义 *********************/
typedef enum {
    SSD1315_OK   = 0,
    SSD1315_ERR  = 1
} SSD1315_StatusTypeDef;

/********************* 显存定义 *********************/
// 显存数组：[页][列]，4页×60列
extern uint8_t OLED_GRAM[SSD1315_PAGE_NUM][SSD1315_WIDTH];

/********************* 函数声明 *********************/
// 初始化SSD1315
SSD1315_StatusTypeDef SSD1315_Init(void);
// 清屏
void SSD1315_Clear(void);
// 更新显存到屏幕
void SSD1315_Refresh(void);
// 画点（x:0~59, y:0~31, color:0=黑,1=白）
void SSD1315_DrawPoint(uint8_t x, uint8_t y, uint8_t color);
// 显示单个字符（x:列起始, y:行起始(0/8/16/24), ch:字符, color:0/1）
void SSD1315_ShowChar(uint8_t x, uint8_t y, char ch, uint8_t color);
// 显示字符串（x:列起始, y:行起始(0/8/16/24), str:字符串, color:0/1）
void SSD1315_ShowString(uint8_t x, uint8_t y, char *str, uint8_t color);

#ifdef __cplusplus
}
#endif

#endif /* __OLED_H__ */
