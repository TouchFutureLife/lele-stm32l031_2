# Project Guidelines

## 工程定位
- 本工程由 STM32CubeMX 生成，目标 MCU 为 STM32L031 系列。
- 关键配置来源：`lele-stm32l031.ioc`。
- 主工程文件（Keil5/uVision）：`MDK-ARM/lele-stm32l031.uvprojx`。

## 语言与工具链
- 代码语言：C（HAL 风格）。
- 编译/调试环境：Keil MDK-ARM v5（uVision5）。
- 启动与链接相关文件位于 `MDK-ARM/`，不要随意改动启动文件。

## 开发框架
- 开发模式：裸机（无 RTOS）。
- 依据：`Core/Inc/stm32l0xx_hal_conf.h` 中 `USE_RTOS` 为 `0U`。
- 禁止引入任务调度器、线程抽象或 RTOS 依赖。

## 修改边界（必须遵守）
- 对 CubeMX 生成文件，仅在 `/* USER CODE BEGIN */` 与 `/* USER CODE END */` 区域内修改业务逻辑。
- 非必要不要手改自动生成初始化代码（如 `MX_*_Init`、MSP、中断框架）。
- 外设改动优先通过 `.ioc` 重新生成，再合并 USER CODE 区域。

## 目录约定
- 应用编排与主循环：`Core/Src/main.c`。
- 外设初始化：`Core/Src/{adc,dma,gpio,i2c,lptim,rtc,usart}.c`。
- 头文件接口：`Core/Inc/`。
- HAL/CMSIS 驱动：`Drivers/`。

## 构建与验证
- 默认构建入口：`MDK-ARM/lele-stm32l031.uvprojx`。
- 产物目录：`MDK-ARM/lele-stm32l031/`（如 `.axf`、`.hex`、`.map`）。
- 提交前最小检查：
  - Keil5 工程可成功编译；
  - 无新增编译告警/错误；
  - 关键外设功能路径不回归（GPIO/I2C/ADC/RTC/UART）。

## 代码变更原则
- 保持现有命名与 HAL 使用风格，做最小必要改动。
- 不新增与本工程无关的框架、脚手架或目录。
- 若需求与以上规则冲突，优先在变更说明中明确冲突点与取舍理由。