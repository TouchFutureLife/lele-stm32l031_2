/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
  /* USER CODE END Header */
  /* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "adc.h"
#include "dma.h"
#include "i2c.h"
#include "lptim.h"
#include "rtc.h"
#include "usart.h"
#include "gpio.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include <stdio.h>
#include <stdarg.h>
#include "oled.h"
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */
typedef struct {
  uint16_t raw_vbat;
  uint16_t raw_vref;
  uint16_t raw_ext_temp;
  uint16_t raw_int_temp;
  uint32_t vdda_mv;
  uint32_t vbat_mv;
  uint32_t vref_mv;
  int32_t  temp_int_c_x100; /* hundredths of deg C */
} adc_sample_t;
/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
#define APP_LED_BLINK_PERIOD_MS   1000U
#define BUTTON_DEBOUNCE_MS        50U
#define BUTTON_LONG_PRESS_MS      1500U
#define VREF_EXT_MV               1250U
#define ADC_FULL_SCALE            4095U
#define TS_CAL1_ADDR              ((uint16_t *)0x1FF8007A)
#define TS_CAL2_ADDR              ((uint16_t *)0x1FF8007E)
#define TS_CAL_VREF_MV            3000U
#define ADC_CHANNEL_COUNT         4U
#define ADC_DMA_SAMPLES           8U
#define EMA_ALPHA_NUM             1U
#define EMA_ALPHA_DEN             4U
/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/

/* USER CODE BEGIN PV */
static volatile uint16_t adc_dma_buf[ADC_CHANNEL_COUNT * ADC_DMA_SAMPLES];
static volatile uint8_t adc_busy = 0;
static volatile uint8_t adc_ready = 0;
static uint8_t adc_filter_init = 0;

static volatile uint8_t rtc_wakeup_flag = 0;
static volatile uint8_t button_irq_flag = 0;
static uint8_t button_pressed = 0;
static uint32_t button_press_tick = 0;
static uint8_t button_short_press = 0;
static uint8_t button_long_press = 0;

static uint32_t last_led_toggle = 0;
static adc_sample_t latest_sample = { 0 };
static uint8_t display_page = 0; /* 0: voltage, 1: temperature */

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);

static void App_SampleAdc(void);
static int32_t App_ComputeInternalTempC_x100(uint16_t raw_ts, uint32_t vdda_mv);
static void App_Log(const char* fmt, ...);
/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

static void App_UpdateDisplay(void)
{
  /* TODO: draw font; for now just clear and log */
  ssd1315_fill(0x05);

  if(display_page == 0) {
    App_Log("display page: voltage, vbat=%lumV, vdda=%lumV, vref=%lumV\r\n",
      (unsigned long)latest_sample.vbat_mv,
      (unsigned long)latest_sample.vdda_mv,
      (unsigned long)latest_sample.vref_mv);
  } else {
    App_Log("display page: temperature, int=%ld.%02ldC ext_raw=%u\r\n",
                (long)(latest_sample.temp_int_c_x100 / 100),
                (long)(latest_sample.temp_int_c_x100 % 100),
                latest_sample.raw_ext_temp);
  }
}

static void App_Log(const char* fmt, ...)
{
  char buf[256];
  int len = 0;
  uint32_t tick = HAL_GetTick();
  len = snprintf(buf, sizeof(buf), "[%lu] ", (unsigned long)tick);

  if(len < 0 || len >= (int)sizeof(buf)) {
    return;
  }

  va_list args;
  va_start(args, fmt);
  int rem = (int)sizeof(buf) - len;
  int n = vsnprintf(&buf[len], (size_t)rem, fmt, args);
  va_end(args);

  if(n < 0) {
    return;
  }

  int total = len + n;
  if(total > (int)sizeof(buf)) {
    total = sizeof(buf);
  }

  HAL_UART_Transmit(&huart2, (uint8_t*)buf, (uint16_t)total, 100);
}


static void App_SampleAdc(void)
{
  if(adc_busy) {
    return; /* Avoid overlapping DMA transfers */
  }

  adc_ready = 0;
  adc_busy = 1;

  if(HAL_ADC_Start_DMA(&hadc, (uint32_t*)adc_dma_buf, ADC_CHANNEL_COUNT * ADC_DMA_SAMPLES) != HAL_OK) {
    adc_busy = 0;
  }
}

static int32_t App_ComputeInternalTempC_x100(uint16_t raw_ts, uint32_t vdda_mv)
{
  uint32_t ts_cal1 = (uint32_t)(*TS_CAL1_ADDR);
  uint32_t ts_cal2 = (uint32_t)(*TS_CAL2_ADDR);
  if(ts_cal2 <= ts_cal1) {
    return 0;
  }

  /* scale raw reading to calibration voltage */
  uint32_t ts_scaled = (uint32_t)raw_ts * vdda_mv / TS_CAL_VREF_MV;
  int32_t numerator = (int32_t)(ts_scaled - ts_cal1) * 100 * (110 - 30);
  int32_t temp_c_x100 = numerator / (int32_t)(ts_cal2 - ts_cal1) + 30 * 100;
  return temp_c_x100;
}

void HAL_ADC_ConvCpltCallback(ADC_HandleTypeDef* hadc_handle)
{
  if(hadc_handle != &hadc) {
    return;
  }

  /* Stop DMA after normal-mode transfer completes */
  HAL_ADC_Stop_DMA(hadc_handle);

  adc_sample_t sample = { 0 };
  uint32_t sum_vbat = 0;
  uint32_t sum_vref = 0;
  uint32_t sum_ext = 0;
  uint32_t sum_int = 0;

  for(uint32_t i = 0; i < ADC_DMA_SAMPLES; i++) {
    uint32_t base = i * ADC_CHANNEL_COUNT;
    sum_vbat += adc_dma_buf[base + 0];
    sum_vref += adc_dma_buf[base + 1];
    sum_ext += adc_dma_buf[base + 2];
    sum_int += adc_dma_buf[base + 3];
  }

  sample.raw_vbat = (uint16_t)(sum_vbat / ADC_DMA_SAMPLES);
  sample.raw_vref = (uint16_t)(sum_vref / ADC_DMA_SAMPLES);
  sample.raw_ext_temp = (uint16_t)(sum_ext / ADC_DMA_SAMPLES);
  sample.raw_int_temp = (uint16_t)(sum_int / ADC_DMA_SAMPLES);

  if(sample.raw_vref > 0) {
    sample.vdda_mv = (uint32_t)VREF_EXT_MV * ADC_FULL_SCALE / sample.raw_vref;
  } else {
    sample.vdda_mv = 3000U;
  }

  sample.vbat_mv = (uint32_t)sample.raw_vbat * sample.vdda_mv * 2 / ADC_FULL_SCALE;
  sample.temp_int_c_x100 = App_ComputeInternalTempC_x100(sample.raw_int_temp, sample.vdda_mv);

  /* Simple EMA to smooth noise */
  latest_sample.raw_vbat = sample.raw_vbat;
  latest_sample.raw_vref = sample.raw_vref;
  latest_sample.raw_ext_temp = sample.raw_ext_temp;
  latest_sample.raw_int_temp = sample.raw_int_temp;
  latest_sample.vdda_mv = sample.vdda_mv;
  latest_sample.vbat_mv = sample.vbat_mv;
  latest_sample.vref_mv = VREF_EXT_MV;
  latest_sample.temp_int_c_x100 = sample.temp_int_c_x100;

  adc_ready = 1;
  adc_busy = 0;
}

void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin)
{
  if(GPIO_Pin == PWRKEYIN_Pin) {
    button_irq_flag = 1;
  }
}
static void App_HandleButton(void)
{
  if(button_irq_flag) {
    button_irq_flag = 0;
    if(!button_pressed) {
      button_pressed = 1;
      button_press_tick = HAL_GetTick();
    }
  }

  if(button_pressed) {
    /* If released */
    if(HAL_GPIO_ReadPin(PWRKEYIN_GPIO_Port, PWRKEYIN_Pin) == GPIO_PIN_SET) {
      uint32_t duration = HAL_GetTick() - button_press_tick;
      button_pressed = 0;
      if(duration >= BUTTON_DEBOUNCE_MS) {
        if(duration >= BUTTON_LONG_PRESS_MS) {
          button_long_press = 1;
        } else {
          button_short_press = 1;
        }
      }
    }
  }
}

static void App_HandleLed(uint32_t now)
{
  if((now - last_led_toggle) >= APP_LED_BLINK_PERIOD_MS) {
    last_led_toggle = now;
    HAL_GPIO_TogglePin(LED_G_GPIO_Port, LED_G_Pin);
  }
}

static void App_Init(void)
{
  /* Latch external power */
  HAL_GPIO_WritePin(POWER_CTL_GPIO_Port, POWER_CTL_Pin, GPIO_PIN_SET);

  /* Enable EXTI for PWRKEYIN (PA1 on EXTI1) */
  HAL_NVIC_SetPriority(EXTI0_1_IRQn, 1, 0);
  HAL_NVIC_EnableIRQ(EXTI0_1_IRQn);

  /* Init OLED */
  if(ssd1315_init() != SSD1315_OK) {
    App_Log("oled init failed\r\n");
  } else {
    App_Log("oled init ok\r\n");
  }

  /* Start an initial ADC sampling */
  App_SampleAdc();

  /* Log boot */
  App_Log("boot: app init complete\r\n");
}

static void App_Run(void)
{
  uint32_t now = HAL_GetTick();

  App_HandleButton();
  App_HandleLed(now);

  if(adc_ready) {
    adc_ready = 0;
    App_Log("adc: vbat_raw=%u vref_raw=%u ext_raw=%u int_raw=%u vdda=%lumV vbat=%lumV vref=%lumV tint=%ld.%02ldC\r\n",
            latest_sample.raw_vbat,
            latest_sample.raw_vref,
            latest_sample.raw_ext_temp,
            latest_sample.raw_int_temp,
            (unsigned long)latest_sample.vdda_mv,
            (unsigned long)latest_sample.vbat_mv,
              (unsigned long)latest_sample.vref_mv,
            (long)(latest_sample.temp_int_c_x100 / 100),
            (long)(latest_sample.temp_int_c_x100 % 100));
    App_UpdateDisplay();
    // App_SampleAdc();
  }

  if(rtc_wakeup_flag) {
    rtc_wakeup_flag = 0;
    App_SampleAdc();
  }

  if(button_short_press) {
    button_short_press = 0;
    display_page ^= 1U;
    App_Log("button short: toggle page=%u\r\n", display_page);
    // App_UpdateDisplay();
  }

  if(button_long_press) {
    button_long_press = 0;
    App_Log("button long: no-op placeholder\r\n");
  }
}



/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{

  /* USER CODE BEGIN 1 */

  /* USER CODE END 1 */

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_DMA_Init();
  MX_ADC_Init();
  MX_I2C1_Init();
  MX_LPTIM1_Init();
  MX_RTC_Init();
  MX_USART2_UART_Init();
  /* USER CODE BEGIN 2 */
  App_Init();
  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while(1) {
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
    App_Run();
  }
  /* USER CODE END 3 */
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = { 0 };
  RCC_ClkInitTypeDef RCC_ClkInitStruct = { 0 };
  RCC_PeriphCLKInitTypeDef PeriphClkInit = { 0 };

  /** Configure the main internal regulator output voltage
  */
  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI | RCC_OSCILLATORTYPE_LSI;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
  RCC_OscInitStruct.LSIState = RCC_LSI_ON;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSI;
  RCC_OscInitStruct.PLL.PLLMUL = RCC_PLLMUL_3;
  RCC_OscInitStruct.PLL.PLLDIV = RCC_PLLDIV_2;
  if(HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK) {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK
    | RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV1;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

  if(HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_1) != HAL_OK) {
    Error_Handler();
  }
  PeriphClkInit.PeriphClockSelection = RCC_PERIPHCLK_USART2 | RCC_PERIPHCLK_I2C1
    | RCC_PERIPHCLK_RTC | RCC_PERIPHCLK_LPTIM1;
  PeriphClkInit.Usart2ClockSelection = RCC_USART2CLKSOURCE_PCLK1;
  PeriphClkInit.I2c1ClockSelection = RCC_I2C1CLKSOURCE_PCLK1;
  PeriphClkInit.RTCClockSelection = RCC_RTCCLKSOURCE_LSI;
  PeriphClkInit.LptimClockSelection = RCC_LPTIM1CLKSOURCE_PCLK;

  if(HAL_RCCEx_PeriphCLKConfig(&PeriphClkInit) != HAL_OK) {
    Error_Handler();
  }
}

/* USER CODE BEGIN 4 */

/* USER CODE END 4 */

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* User can add his own implementation to report the HAL error return state */
  __disable_irq();
  while(1) {
  }
  /* USER CODE END Error_Handler_Debug */
}
#ifdef USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t* file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* User can add his own implementation to report the file name and line number,
     ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
     /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
