# 开发说明文档（STM32L031）

## 1. 项目概览
- 工程来源：STM32CubeMX 生成模板。
- 目标 MCU：STM32L031（Keil 工程目标为 STM32L031K6Ux）。
- 开发框架：裸机（无 RTOS）。
- 代码风格：STM32 HAL C 工程。

参考文件：
- `lele-stm32l031.ioc`
- `MDK-ARM/lele-stm32l031.uvprojx`
- `Core/Inc/stm32l0xx_hal_conf.h`

---

## 2. 开发环境与构建

### 2.1 工具链
- IDE/编译环境：Keil MDK-ARM v5（uVision5）。
- 默认构建入口：`MDK-ARM/lele-stm32l031.uvprojx`。
- 产物目录：`MDK-ARM/lele-stm32l031/`（`.axf` / `.hex` / `.map`）。

### 2.2 构建方式
1. 使用 Keil5 打开 `MDK-ARM/lele-stm32l031.uvprojx`。
2. 选择目标 `lele-stm32l031`。
3. 执行 Build。

参考文件：
- `MDK-ARM/lele-stm32l031.uvprojx`
- `MDK-ARM/lele-stm32l031/lele-stm32l031.build_log.htm`

---

## 3. 目录结构与职责
- `Core/Src/main.c`：应用主流程、采样调度、按键逻辑、显示更新。
- `Core/Src/adc.c`：ADC 与 DMA 配置。
- `Core/Src/oled.c`：SSD1315 OLED 驱动（I2C）。
- `Core/Src/gpio.c`：GPIO 与按键中断引脚初始化。
- `Core/Src/rtc.c`：RTC 配置与 Wakeup 定时。
- `Core/Src/stm32l0xx_it.c`：中断入口与 HAL IRQ 分发。
- `Core/Inc/`：模块头文件与引脚定义。
- `Drivers/`：HAL/CMSIS 驱动。

---

## 4. 系统启动与运行流程
1. `main()` 中完成 HAL 初始化、时钟配置、外设初始化。
2. 调用 `App_Init()`：上电保持、OLED 初始化、首次 ADC 采样、启动日志。
3. 进入 `while(1)`，循环执行 `App_Run()`。

`App_Run()` 当前包含：
- 按键状态处理（短按/长按）。
- LED 周期闪烁。
- RTC 唤醒事件触发 ADC 采样（4 秒周期）。
- ADC 完成后计算温度、打印日志、刷新 OLED。
- 处理 `rtc_wakeup_flag` 触发的采样。
- 空闲时进入 Sleep（`WFI`，并在进入前后挂起/恢复 SysTick）。

参考文件：
- `Core/Src/main.c`

---

## 5. 已实现功能说明

### 5.1 ADC 采样与温度计算
- 采样通道：VBAT、VREF、外部温度、内部温度。
- 采样方式：DMA 批量采样后求均值。
- 外部温度：基于 PT1000 阻值表 + 插值换算。
- 内部温度：基于校准地址 `TS_CAL1/TS_CAL2` 计算。

参考文件：
- `Core/Src/adc.c`
- `Core/Src/main.c`

### 5.2 OLED 显示
- 驱动芯片：SSD1315。
- 主要流程：清屏 -> 绘制 -> `SSD1315_Refresh_Gram()` 整屏刷新。
- 当前显示内容：外部温度（大字体）。

参考文件：
- `Core/Src/oled.c`
- `Core/Inc/oled.h`
- `Core/Src/main.c`

### 5.3 按键功能
- 按键中断：`PWRKEYIN` 下降沿触发 EXTI。
- 主循环完成去抖与时长判断：
  - 短按：清除已锁存最高体温并重新开始锁存。
  - 长按：执行关断电源控制引脚。

参考文件：
- `Core/Src/gpio.c`
- `Core/Src/stm32l0xx_it.c`
- `Core/Src/main.c`

---

## 6. 中断与事件协作模型
- IRQ 中仅做 HAL 分发/置位，业务在主循环消费，避免中断内重逻辑。
- 关键回调：
  - `HAL_ADC_ConvCpltCallback()`：搬运并计算采样结果，置 `adc_ready`。
  - `HAL_GPIO_EXTI_Callback()`：置 `button_irq_flag`。
  - `HAL_RTCEx_WakeUpTimerEventCallback()`：置 `rtc_wakeup_flag`。
- 主循环根据标志位执行业务逻辑。

参考文件：
- `Core/Src/main.c`
- `Core/Src/stm32l0xx_it.c`

---

## 7. 低功耗设计（下一步实施）

### 7.1 当前瓶颈
- 已进入 Sleep，但仍有进一步优化空间（如更深层 Stop 模式）。
- 周期日志与 OLED 整屏刷新占用较高。
- RTC 已改为中断唤醒，`rtc_wakeup_flag` 链路已闭环。

参考文件：
- `Core/Src/main.c`
- `Core/Src/rtc.c`
- `Core/Src/stm32l0xx_it.c`

### 7.2 目标策略（已确定）
- 采样周期改为 4 秒。
- OLED 默认熄屏，仅调试模式点亮。
- 分阶段推进：
  1) 先做 Sleep + 降低日志/显示占空比；
  2) 补齐 RTC 中断唤醒闭环；
  3) 评估 Stop 模式与外设恢复。

---

## 8. 最高体温锁存策略（下一步实施）

### 8.1 业务目标
- 防止测温探头离体后被环境温度覆盖，保留一次测量中的最高体温。

### 8.2 已确认规则
- 开机后始终开始锁存最高值。
- 短按按键可清除锁存值并重新开始。
- 显示规则：在体显示实时温度，离体显示锁存最高值。
- 在体/离体判定：固定阈值 + 回差（避免抖动）。

### 8.3 建议实现点
- 在 `main.c` 增加峰值状态变量（如 `peak_temp_c_x100`、`peak_valid`、`wear_state`）。
- 在 ADC 数据就绪分支更新峰值与在体状态。
- 将短按行为从“翻页”改为“清除锁存”。

参考文件：
- `Core/Src/main.c`

---

## 9. 开发与修改边界
- 对 CubeMX 生成文件，仅在 `/* USER CODE BEGIN */` 与 `/* USER CODE END */` 区域内改业务逻辑。
- 外设结构性变更优先通过 `.ioc` 修改后重新生成，再合并 USER CODE。
- 非必要不改启动文件和自动生成初始化框架。

参考文件：
- `.github/copilot-instructions.md`
- `lele-stm32l031.ioc`

---

## 10. 最小回归检查清单
- 构建：Keil5 编译通过，无新增告警/错误。
- 功能：
  - ADC 周期采样正常；
  - 温度计算稳定；
  - OLED 显示与按键响应正常；
  - 长按关机路径正常。
- 低功耗阶段新增检查：
  - 空闲电流、周期采样电流、按键唤醒电流三场景对比；
  - RTC 唤醒链路稳定；
  - 最高体温锁存在离体场景不被覆盖。

---

## 11. 待补充信息（建议后续补齐）
- 硬件接线图/引脚说明（传感器与 OLED 实际连线）。
- 烧录/调试器规范（ST-Link/J-Link 操作步骤）。
- 产测与标定流程（PT1000 校准策略、阈值标定方法）。
- 版本发布规范（分支、Tag、变更记录模板）。
