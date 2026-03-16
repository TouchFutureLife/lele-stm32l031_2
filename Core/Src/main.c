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
#include <math.h>
#include <stdlib.h>
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
  uint32_t temp_ext_c_pt1000_mv;
  int32_t  temp_int_c_x100; /* hundredths of deg C */
  int32_t  temp_ext_c_x100; /* hundredths of deg C, computed from pt1000 */
} adc_sample_t;
/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
#define APP_LED_BLINK_PERIOD_MS   1000U
#define VREF_EXT_MV               1250U
#define ADC_FULL_SCALE            4095U
#define TS_CAL1_ADDR              ((uint16_t *)0x1FF8007A)
#define TS_CAL2_ADDR              ((uint16_t *)0x1FF8007E)
#define TS_CAL_VREF_MV            3000U
#define ADC_CHANNEL_COUNT         4U
#define ADC_DMA_SAMPLES           8U
#define EMA_ALPHA_NUM             1U
#define EMA_ALPHA_DEN             4U

#define ADC_REF_VOLTAGE             VREF_EXT_MV
#define DISPLAY_ENABLE              1U
#define APP_KEY_WAKEUP_ENABLE       0U
#define TEMP_WEAR_ON_THRESHOLD_X100 3500
#define TEMP_WEAR_OFF_THRESHOLD_X100 3450
#define INVALID_TEMP_X100           (-99900)

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/

/* USER CODE BEGIN PV */
static volatile uint16_t adc_dma_buf[ADC_CHANNEL_COUNT * ADC_DMA_SAMPLES];
static volatile uint8_t adc_busy = 0;
static volatile uint8_t adc_ready = 0;
// adc_filter_init reserved for future smoothing; unused currently

static volatile uint8_t rtc_wakeup_flag = 0;

static uint32_t last_led_toggle = 0;
static adc_sample_t latest_sample = { 0 };
static int32_t peak_temp_c_x100 = INVALID_TEMP_X100;
static uint8_t peak_temp_valid = 0;
static uint8_t wear_state_on_body = 0;
#if DISPLAY_ENABLE
static int32_t display_temp_c_x100 = INVALID_TEMP_X100;
#endif


/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);

static void App_SampleAdc(void);
static int32_t App_ComputeInternalTempC_x100(uint16_t raw_ts, uint32_t vdda_mv);
static void App_Log(const char* fmt, ...);
#if DISPLAY_ENABLE
static void App_UpdateDisplay(void);
#endif
static void App_UpdateWearState(int32_t temp_c_x100);
#if DISPLAY_ENABLE
static int32_t App_GetDisplayTemp(void);
#endif
static void App_EnterStopMode(void);
/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

// -------------------------- 1. 各温度对应的阻值数组（基于您提供的矩阵数据） --------------------------
// 32℃对应的阻值列表
static const float pt1000_32[] = {
    1124.474, 1124.861, 1125.248, 1125.636, 1126.023,
    1126.41,  1126.797, 1127.184, 1127.571, 1127.958
};

// 33℃对应的阻值列表
static const float pt1000_33[] = {
  1128.345, 1128.732, 1129.119, 1129.893, 1130.127,
  1130.28,  1130.667, 1131.054, 1131.441, 1131.82
};

// 34℃对应的阻值列表
static const float pt1000_34[] = {
    1132.215, 1132.602, 1132.988, 1133.375, 1133.762,
    1134.149, 1134.536, 1134.923, 1135.309, 1135.69
};

// 35℃对应的阻值列表
static const float pt1000_35[] = {
    1136.083, 1136.47,  1136.857, 1137.243, 1137.63,
    1138.017, 1138.404, 1138.79,  1139.177, 1139.564
};

// 36℃对应的阻值列表
static const float pt1000_36[] = {
    1139.95,  1140.337, 1140.724, 1141.11,  1141.497,
    1141.884, 1142.27,  1142.657, 1143.043, 1143.43
};

// 37℃对应的阻值列表
static const float pt1000_37[] = {
    1143.817, 1144.203, 1144.59,  1144.976, 1145.363,
    1145.749, 1146.136, 1146.522, 1146.909, 1147.295
};

// 38℃对应的阻值列表
static const float pt1000_38[] = {
    1147.681, 1148.068, 1148.454, 1148.841, 1149.227,
    1149.614, 1150.0,   1150.386, 1150.773, 1151.159
};

// 39℃对应的阻值列表
static const float pt1000_39[] = {
    1151.545, 1151.932, 1152.318, 1152.704, 1153.091,
    1153.477, 1153.86,  1154.249, 1154.636, 1155.02
};

// 40℃对应的阻值列表
static const float pt1000_40[] = {
    1155.408, 1155.794, 1156.18,  1156.567, 1156.953,
    1157.339, 1157.725, 1158.111, 1158.497, 1158.883
};

// 41℃对应的阻值列表
static const float pt1000_41[] = {
    1159.27,  1159.656, 1160.042, 1160.428, 1160.814,
    1161.2,   1161.586, 1161.972, 1162.358, 1162.744
};

// 42℃对应的阻值列表
static const float pt1000_42[] = {
    1163.13,  1163.516, 1163.902, 1164.288, 1164.674,
    1165.06,  1165.446, 1165.831, 1166.217, 1166.6
};

// 43℃对应的阻值列表
static const float pt1000_43[] = {
    1166.989, 1167.376, 1167.761, 1168.147, 1168.532,
    1168.918, 1169.304, 1169.69,  1170.076, 1170.46
};

// 44℃对应的阻值列表
static const float pt1000_44[] = {
    1170.847, 1171.233, 1171.619, 1172.004, 1172.39,
    1172.776, 1173.161, 1173.547, 1173.933, 1174.318
};

// 45℃对应的阻值列表
static const float pt1000_45[] = {
    1174.704, 1175.09,  1175.475, 1175.861, 1176.247,
    1176.632, 1177.018, 1177.403, 1177.789, 1178.174
};


// -------------------------- 2. 温度-阻值数组映射表（核心关联结构） --------------------------
typedef struct {
  int8_t temp;               // 温度（℃，32~45）
  const float* resistances;  // 对应温度的阻值数组
  uint8_t res_count;         // 阻值数组的元素数量
} PT1000_TempResMap;

// 32℃~45℃ 温度-阻值映射表（整合上述数组）
const PT1000_TempResMap pt1000_temp_res_map[] = {
    {32, pt1000_32, sizeof(pt1000_32) / sizeof(float)},
    {33, pt1000_33, sizeof(pt1000_33) / sizeof(float)},
    {34, pt1000_34, sizeof(pt1000_34) / sizeof(float)},
    {35, pt1000_35, sizeof(pt1000_35) / sizeof(float)},
    {36, pt1000_36, sizeof(pt1000_36) / sizeof(float)},
    {37, pt1000_37, sizeof(pt1000_37) / sizeof(float)},
    {38, pt1000_38, sizeof(pt1000_38) / sizeof(float)},
    {39, pt1000_39, sizeof(pt1000_39) / sizeof(float)},
    {40, pt1000_40, sizeof(pt1000_40) / sizeof(float)},
    {41, pt1000_41, sizeof(pt1000_41) / sizeof(float)},
    {42, pt1000_42, sizeof(pt1000_42) / sizeof(float)},
    {43, pt1000_43, sizeof(pt1000_43) / sizeof(float)},
    {44, pt1000_44, sizeof(pt1000_44) / sizeof(float)},
    {45, pt1000_45, sizeof(pt1000_45) / sizeof(float)}
};

// 映射表的总条目数（自动计算，避免硬编码错误）
#define PT1000_MAP_ENTRY_COUNT (sizeof(pt1000_temp_res_map) / sizeof(PT1000_TempResMap))

// -------------------------- 2. 核心函数：阻值转温度（0.1℃精度） --------------------------
/**
 * @brief 阻值转温度（精度0.1℃）
 * @param target_res: 采样的PT1000阻值（Ω）
 * @return 温度值（℃，保留1位小数）；返回-999.0表示阻值超出32~45℃对应范围
 */
float pt1000_res_to_temp_01deg(const float target_res)
{
  // 定义相邻原始点（R_prev/T_prev：前一个0.1℃步进点；R_next/T_next：后一个0.1℃步进点）
  float R_prev = 0.0f, T_prev = 0.0f;
  float R_next = 0.0f, T_next = 0.0f;
  // 全局遍历所有原始阻值点，找到目标阻值的相邻区间
  for(uint8_t i = 0; i < PT1000_MAP_ENTRY_COUNT; i++) {
    const PT1000_TempResMap* entry = &pt1000_temp_res_map[i];
    for(uint8_t j = 0; j < entry->res_count; j++) {
      float current_R = entry->resistances[j];
      // 原始点的温度：基准温度 + j×0.1℃（如32.0 + 0×0.1=32.00，32.0+1×0.1=32.10...）
      float current_T = entry->temp + (j * 0.1f);

      // 1. 精准匹配原始点：直接返回0.01℃精度的温度（无插值）
      if(fabs(current_R - target_res) < 1e-6) {  // 浮点精度容错（1e-6Ω）
        return roundf(current_T * 100) / 100;  // 强制保留2位小数
      }

      // 2. 更新前一个点（≤目标阻值的最大阻值点）
      if(current_R <= target_res && current_R > R_prev) {
        R_prev = current_R;
        T_prev = current_T;
      }

      // 3. 更新后一个点（≥目标阻值的最小阻值点）
      if(current_R >= target_res) {
        if(R_next == 0.0f || current_R < R_next) {
          R_next = current_R;
          T_next = current_T;
        }
      }
    }
  }

  // 校验：阻值超出32~45℃范围（无有效相邻点）
  if(R_prev == 0.0f || R_next == 0.0f) {
    return -999.00f;
  }

  // 4. 二级线性插值：在0.1℃区间内细分到0.01℃
  // 公式：Tx = T_prev + (T_next - T_prev) × (Rx - R_prev)/(R_next - R_prev)
  // T_next - T_prev = 0.1℃，因此结果会自然细分到0.01℃
  float temp_delta = T_next - T_prev;       // 固定为0.1℃（原始点步进）
  float res_delta = R_next - R_prev;        // 原始点的阻值差
  if(res_delta <= 0.0f) {
    return -999.00f; // 数据异常（非递增或重复）
  }
  float res_offset = target_res - R_prev;   // 目标阻值相对前一个点的偏移
  float interpolated_T = T_prev + (temp_delta * res_offset) / res_delta;

  // 5. 四舍五入保留2位小数（确保0.01℃精度，避免浮点误差）
  interpolated_T = roundf(interpolated_T * 100) / 100;


  return interpolated_T;
}

#if DISPLAY_ENABLE
static void App_UpdateDisplay(void)
{
  SSD1315_Clear();
  char buff[16];
  snprintf(buff, sizeof(buff), "%.2fo", display_temp_c_x100 * 0.01f);
  SSD1315_ShowBigText(1, 8, buff, 1);
  SSD1315_Refresh_Gram();
}
#endif

static void App_UpdateWearState(int32_t temp_c_x100)
{
  if(wear_state_on_body) {
    if(temp_c_x100 <= TEMP_WEAR_OFF_THRESHOLD_X100) {
      wear_state_on_body = 0;
    }
  } else {
    if(temp_c_x100 >= TEMP_WEAR_ON_THRESHOLD_X100) {
      wear_state_on_body = 1;
    }
  }
}

#if DISPLAY_ENABLE
static int32_t App_GetDisplayTemp(void)
{
  if(wear_state_on_body) {
    return latest_sample.temp_ext_c_x100;
  }

  if(peak_temp_valid) {
    return peak_temp_c_x100;
  }

  return latest_sample.temp_ext_c_x100;
}
#endif

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
  sample.temp_ext_c_pt1000_mv = (uint32_t)sample.raw_ext_temp * sample.vdda_mv / ADC_FULL_SCALE;

  /* Simple EMA to smooth noise */
  latest_sample.raw_vbat = sample.raw_vbat;
  latest_sample.raw_vref = sample.raw_vref;
  latest_sample.raw_ext_temp = sample.raw_ext_temp;
  latest_sample.raw_int_temp = sample.raw_int_temp;
  latest_sample.vdda_mv = sample.vdda_mv;
  latest_sample.vbat_mv = sample.vbat_mv;
  latest_sample.vref_mv = VREF_EXT_MV;
  latest_sample.temp_int_c_x100 = sample.temp_int_c_x100;
  latest_sample.temp_ext_c_pt1000_mv = sample.temp_ext_c_pt1000_mv;

  adc_ready = 1;
  adc_busy = 0;
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
  GPIO_InitTypeDef gpio_init = { 0 };

  /* Latch external power */
  HAL_GPIO_WritePin(POWER_CTL_GPIO_Port, POWER_CTL_Pin, GPIO_PIN_SET);

  /* Select wake-up source policy at runtime init. */
#if (APP_KEY_WAKEUP_ENABLE == 0U)
  /* RTC is the only wake-up source. */
  HAL_NVIC_DisableIRQ(EXTI0_1_IRQn);
  gpio_init.Pin = PWRKEYIN_Pin;
  gpio_init.Mode = GPIO_MODE_INPUT;
  gpio_init.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(PWRKEYIN_GPIO_Port, &gpio_init);
  __HAL_GPIO_EXTI_CLEAR_IT(PWRKEYIN_Pin);
#else
  /* Keep key EXTI as an additional wake-up source. */
  __HAL_GPIO_EXTI_CLEAR_IT(PWRKEYIN_Pin);
  HAL_NVIC_ClearPendingIRQ(EXTI0_1_IRQn);
  HAL_NVIC_EnableIRQ(EXTI0_1_IRQn);
#endif

#if DISPLAY_ENABLE
  if(SSD1315_Init() == SSD1315_OK) {
    SSD1315_Clear();
    SSD1315_Refresh_Gram();
  }
#endif
  /* Start an initial ADC sampling */
  App_SampleAdc();

  /* Log boot */
  App_Log("boot: app init complete\r\n");
}

static void App_Run(void)
{
  uint32_t now = HAL_GetTick();

  App_HandleLed(now);

  if(adc_ready) {
    adc_ready = 0;

    float tmp_res = (latest_sample.raw_ext_temp * 1.0f / latest_sample.raw_vref) * 1000.0f / 82 + 1136;
    float temperature = pt1000_res_to_temp_01deg(tmp_res);
    //temperature转换到temp_ext_c_x100
    latest_sample.temp_ext_c_x100 = (int32_t)(temperature * 100);

    if(latest_sample.temp_ext_c_x100 > INVALID_TEMP_X100) {
      App_UpdateWearState(latest_sample.temp_ext_c_x100);
      if(!peak_temp_valid || latest_sample.temp_ext_c_x100 > peak_temp_c_x100) {
        peak_temp_c_x100 = latest_sample.temp_ext_c_x100;
        peak_temp_valid = 1;
      }
    }

    App_Log("adc: ext=%ld.%02ldC peak=%ld.%02ldC wear=%u vbat=%lumV\r\n",
            (long)(latest_sample.temp_ext_c_x100 / 100),
            (long)labs(latest_sample.temp_ext_c_x100 % 100),
            (long)(peak_temp_c_x100 / 100),
            (long)labs(peak_temp_c_x100 % 100),
            (unsigned int)wear_state_on_body,
            (unsigned long)latest_sample.vbat_mv);
#if DISPLAY_ENABLE
  display_temp_c_x100 = App_GetDisplayTemp();
    App_UpdateDisplay();
#endif
  }

  if(rtc_wakeup_flag) {
    rtc_wakeup_flag = 0;
    App_SampleAdc();
  }

  if(!adc_busy && !adc_ready && !rtc_wakeup_flag) {
    App_EnterStopMode();
  }
}

static void App_EnterStopMode(void)
{
  HAL_SuspendTick();
  HAL_PWR_EnterSTOPMode(PWR_LOWPOWERREGULATOR_ON, PWR_STOPENTRY_WFI);
  SystemClock_Config();
  HAL_ResumeTick();
}

void HAL_RTCEx_WakeUpTimerEventCallback(RTC_HandleTypeDef* hrtc_handle)
{
  if(hrtc_handle == &hrtc) {
    rtc_wakeup_flag = 1;
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
  HAL_GPIO_WritePin(GPIOA, POWER_CTL_Pin, GPIO_PIN_SET);
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
