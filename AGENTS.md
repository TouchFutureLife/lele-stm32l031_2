# AGENTS.md - Embedded STM32L031 Project Guidelines

This document provides guidelines for agentic coding agents operating in this repository.

## 1. Project Overview

- **Target MCU**: STM32L031 series (STM32L031K6Ux)
- **Language**: C with STM32 HAL library
- **IDE**: Keil MDK-ARM v5 (uVision5)
- **Framework**: Bare-metal (no RTOS)
- **Configuration Source**: `lele-stm32l031.ioc`

## 2. Build Commands

### Build the Project
```bash
# Open Keil5 and build
# Target: lele-stm32l031
# Entry: MDK-ARM/lele-stm32l031.uvprojx
```

### Build Output Location
- **Artifacts**: `MDK-ARM/lele-stm32l031/`
- **Binary files**: `.axf`, `.hex`, `.map`, `.lst`
- **Object files**: `.o`, `.d`, `.crf`

### Compilation Checks (Pre-commit)
1. Keil5 must compile successfully
2. No new compilation warnings/errors
3. Key peripheral paths must not regress (GPIO/I2C/ADC/RTC/UART)

### Flash/Debug
- Use J-Link or ST-Link via Keil5
- Configuration: `MDK-ARM/lele-stm32l031.uvprojx`

## 3. Code Style Guidelines

### General Principles
- Follow existing HAL usage patterns
- Make minimal necessary changes
- Do not introduce unnecessary frameworks or scaffolding
- Keep code consistent with STM32CubeMX generated style

### Naming Conventions
- **Functions**: `HAL_<Peripheral>_<Action>()` (HAL style)
- **Application functions**: `App_<FunctionName>()` (e.g., `App_Init()`, `App_Run()`)
- **Variables**: camelCase for local, `snake_case` for application-level
- **Constants/Macros**: `UPPER_SNAKE_CASE` with optional prefix (e.g., `APP_`, `ADC_`)
- **Types**: `uint8_t`, `uint16_t`, `uint32_t`, `int32_t` (stdint.h)
- **Booleans**: `uint8_t` or `int` (not `bool` unless C99+)

### Type Usage
- Use fixed-width integer types from `<stdint.h>`
- Temperature values: `int32_t` with `_x100` suffix for centi-degrees
- Raw ADC values: `uint16_t`
- Flags: `volatile uint8_t` for interrupt-communicated flags

### Code Organization

#### File Structure
- **Main application**: `Core/Src/main.c`
- **Peripheral drivers**: `Core/Src/{adc,dma,gpio,i2c,lptim,rtc,usart}.c`
- **Headers**: `Core/Inc/`
- **HAL/CMSIS**: `Drivers/`

#### USER CODE Regions
Only modify code within these markers in CubeMX-generated files:
```c
/* USER CODE BEGIN <Section> */
/* USER CODE END <Section> */
```

Sections include: `Includes`, `PTD`, `PD`, `PM`, `PV`, `PFP`, `0`, `1`, `2`, `3`, `4`, `5`, `6`, `7`, `8`, `9`

### Formatting
- **Indentation**: 2 spaces (match Keil default)
- **Braces**: K&R style
- **Line length**: Keep under 120 characters
- **Comments**: C-style `/* */` for blocks, `//` acceptable for single-line
- **Header guards**: `#ifndef __<FILE>_H` / `#define __<FILE>_H` / `#endif`

### Import Organization
Order (within USER CODE sections):
1. Standard C library: `<stdio.h>`, `<stdarg.h>`, `<stdlib.h>`, `<math.h>`
2. MCU headers: `"main.h"`, `"adc.h"`, `"dma.h"`, etc.
3. Application headers: `"oled.h"`, custom drivers

### Error Handling
- Use `Error_Handler()` for unrecoverable failures
- Check HAL return values: `if (HAL_StatusTypeDef != HAL_OK) Error_Handler();`
- Keep ISR lightweight; set flags for main loop processing

### Interrupt Handling
- ISR should only set flags or perform minimal work
- Business logic in main loop via flag polling
- Key callbacks:
  - `HAL_ADC_ConvCpltCallback()`: Set `adc_ready` flag
  - `HAL_GPIO_EXTI_Callback()`: Set `button_irq_flag`
  - `HAL_RTCEx_WakeUpTimerEventCallback()`: Set `rtc_wakeup_flag`

## 4. Peripheral-Specific Guidelines

### ADC/DMA
- Use DMA for continuous ADC sampling
- Process data in callback, signal main loop via flag
- Temperature calculation: use calibration values from `TS_CAL1_ADDR` / `TS_CAL2_ADDR`

### RTC
- Use RTC Wakeup timer for periodic sampling
- Configure with `HAL_RTCEx_SetWakeUpTimer_IT()` for interrupt mode
- Clear flags in main loop after processing

### I2C (OLED Display)
- Use HAL I2C functions: `HAL_I2C_Mem_Write()`, etc.
- SSD1315 OLED driver in `Core/Src/oled.c`

### GPIO
- Use CubeMX-generated pin definitions from `main.h`
- Keep EXTI callbacks minimal

## 5. Low-Power Considerations

- Use `HAL_SuspendTick()` / `HAL_ResumeTick()` around `WFI` instruction
- Enter Stop mode via `HAL_PWR_EnterSTOPMode()`
- Reconfigure clock after wakeup in `SystemClock_Config()`

## 6. Modification Boundaries

### Must Follow
- Only modify USER CODE sections in CubeMX-generated files
- Re-generate via `.ioc` for peripheral structural changes
- Do not modify startup files (`startup_stm32l031xx.s`)
- Do not introduce RTOS or task schedulers

### Prohibited
- No new frameworks or scaffolding
- No unrelated peripheral drivers
- No changes outside USER CODE regions in generated files

## 7. Testing

- **No unit tests**: This is bare-metal embedded code
- **Hardware testing**: Manual verification via Keil debugger
- **Regression checks**: Verify ADC/RTC/GPIO/UART functionality after changes

## 8. Key Files Reference

| File | Purpose |
|------|---------|
| `lele-stm32l031.ioc` | CubeMX configuration |
| `MDK-ARM/lele-stm32l031.uvprojx` | Keil project |
| `Core/Src/main.c` | Application main loop |
| `Core/Src/rtc.c` | RTC wakeup configuration |
| `Core/Src/adc.c` | ADC/DMA configuration |
| `Core/Inc/stm32l0xx_hal_conf.h` | HAL configuration |
| `docs/development-guide.md` | Full development documentation |

## 9. Commit Message Guidelines

- Use clear, concise descriptions
- Include functional scope (e.g., "Add RTC wakeup", "Fix ADC sampling")
- Reference issue numbers if applicable

## 10. Pre-commit Checklist

- [ ] Code compiles in Keil5 without warnings
- [ ] No new compilation errors
- [ ] Changes limited to USER CODE sections
- [ ] Key peripherals (ADC, RTC, GPIO, UART) functional
- [ ] No RTOS or framework additions
- [ ] Build artifacts ignored (see `.gitignore`)
