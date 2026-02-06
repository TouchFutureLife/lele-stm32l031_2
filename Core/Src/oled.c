#include "oled.h"
#include "i2c.h"
#include "gpio.h"
#include <string.h>

/* SSD1315/SSD1306 compatible minimal driver using HAL I2C */

#define SSD1315_I2C_ADDR    (0x3C << 1) /* 0x78 8-bit */
#define SSD1315_CTRL_CMD    0x00
#define SSD1315_CTRL_DATA   0x40
#define SSD1315_MAX_PAGE    7U
#define SSD1315_MAX_COL     127U

static ssd1315_status_t ssd1315_write_bytes(uint8_t control, uint8_t* data, uint16_t len)
{
  if(HAL_I2C_Master_Transmit(&hi2c1, SSD1315_I2C_ADDR, data, len, 100) == HAL_OK) {
    return SSD1315_OK;
  }
  return SSD1315_ERR;
}

ssd1315_status_t ssd1315_send_cmd(uint8_t cmd)
{
  uint8_t cmd_buf[4] = { 0 };
  cmd_buf[0] = SSD1315_CTRL_CMD;
  cmd_buf[1] = cmd;
  return ssd1315_write_bytes(SSD1315_CTRL_CMD, cmd_buf, 2);
}

ssd1315_status_t ssd1315_send_data(uint8_t data)
{
  return ssd1315_write_bytes(SSD1315_CTRL_DATA, &data, 1);
}

ssd1315_status_t ssd1315_write_page(uint8_t page, uint8_t col_start, uint8_t* data, uint16_t len)
{
  if(page > SSD1315_MAX_PAGE || col_start > SSD1315_MAX_COL) {
    return SSD1315_ERR;
  }

  uint8_t cmds[3];
  cmds[0] = 0xB0 | (page & 0x0F);          /* set page address */
  cmds[1] = 0x00 | (col_start & 0x0F);     /* lower column */
  cmds[2] = 0x10 | ((col_start >> 4) & 0x0F); /* higher column */

  if(ssd1315_write_bytes(SSD1315_CTRL_CMD, cmds, 3) != SSD1315_OK) {
    return SSD1315_ERR;
  }

  return ssd1315_write_bytes(SSD1315_CTRL_DATA, data, len);
}

ssd1315_status_t ssd1315_fill(uint8_t value)
{
  uint8_t buf[16];
  memset(buf, value, sizeof(buf));

  for(uint8_t page = 0; page <= SSD1315_MAX_PAGE; ++page) {
    if(ssd1315_write_page(page, 0, buf, sizeof(buf)) != SSD1315_OK) {
      return SSD1315_ERR;
    }
    /* write remaining columns in chunks */
    for(uint8_t col = sizeof(buf); col <= SSD1315_MAX_COL; col += sizeof(buf)) {
      uint8_t chunk = (uint8_t)((SSD1315_MAX_COL - col + 1 >= sizeof(buf)) ? sizeof(buf) : (SSD1315_MAX_COL - col + 1));
      if(ssd1315_write_page(page, col, buf, chunk) != SSD1315_OK) {
        return SSD1315_ERR;
      }
      if(chunk < sizeof(buf)) {
        break;
      }
    }
  }
  return SSD1315_OK;
}

ssd1315_status_t ssd1315_init(void)
{
  /* Hardware reset via LCD_RST pin */
  HAL_GPIO_WritePin(LCD_RST_GPIO_Port, LCD_RST_Pin, GPIO_PIN_RESET);
  HAL_Delay(200);
  HAL_GPIO_WritePin(LCD_RST_GPIO_Port, LCD_RST_Pin, GPIO_PIN_SET);
  HAL_Delay(500);

  /* Initialization sequence from provided pseudocode */
  uint8_t init_cmds[] = {
      0xAE,
      0x02,
      0x12,
      0x00,
      0xB0,
      0x81,
      0x18,
      0xA0,
      0xA6,
      0xA8,
      0x1F,
      0xC8,
      0xD3,
      0x00,
      0xD5,
      0x80,
      0xD9,
      0xF1,
      0xDA,
      0x12,
      0xDB,
      0x40,
      0x8D,
      0x14,
      0xAD,
      0x30,
      0xAF
  };

  for(uint32_t i = 0; i < sizeof(init_cmds); ++i) {
    if(ssd1315_send_cmd(init_cmds[i]) != SSD1315_OK) {
      return SSD1315_ERR;
    }
  }

  HAL_Delay(200);
  /* Clear display */
  ssd1315_fill(0x05);
  return SSD1315_OK;
}
