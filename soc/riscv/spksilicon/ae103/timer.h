/*
 * Copyright (c) 2026 Sparkle Silicon Technology Corp., Ltd.
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef __RISCV_SPKSILICON_AE103_TIMER_H_
#define __RISCV_SPKSILICON_AE103_TIMER_H_

#include <stdbool.h>

#include <zephyr/device.h>

/**
 * @brief 启动一路 AE103 硬件定时器（16-bit 递减计数）。
 * @param dev  设备句柄（由 DT_INST 宏取得）。
 * @param ch   通道号 0~3。
 * @param load 装载值（递减计数初值）。
 * @param loop true = 自动重装载（循环模式），false = 单次。
 */
void ae103_timer_start(const struct device *dev, uint8_t ch, uint16_t load, bool loop);

/**
 * @brief 停止一路定时器。
 */
void ae103_timer_stop(const struct device *dev, uint8_t ch);

/**
 * @brief 读取某一路当前计数值。
 */
uint16_t ae103_timer_get_count(const struct device *dev, uint8_t ch);

/**
 * @brief 使能一路定时器（置 TCR.EN，保留 LOOP/INT_MSK 位）。
 * @note 对齐固件 TIMER_Enable（TCR |= TIMER_EN）。
 */
void ae103_timer_enable(const struct device *dev, uint8_t ch);

/**
 * @brief 禁用一路定时器（清 TCR.EN，保留 LOOP/INT_MSK 位）。
 * @note 对齐固件 TIMER_Disable（TCR &= ~TIMER_EN）。与 ae103_timer_stop
 *       （整体清零 TCR）不同：disable 仅暂停计数，保留配置可再 enable。
 */
void ae103_timer_disable(const struct device *dev, uint8_t ch);

/**
 * @brief 允许一路定时器中断（清 TCR.INT_MSK）。
 * @note 对齐固件 Timer_Int_Enable（TCR &= ~TIMER_MASK_EN）。
 */
void ae103_timer_int_enable(const struct device *dev, uint8_t ch);

/**
 * @brief 屏蔽一路定时器中断（置 TCR.INT_MSK）。
 * @note 对齐固件 Timer_Int_Disable（TCR |= TIMER_MASK_EN）。
 */
void ae103_timer_int_disable(const struct device *dev, uint8_t ch);

/**
 * @brief 读出某一路中断是否允许。
 * @return true = 中断已允许（INT_MSK 为 0），false = 已屏蔽。
 * @note 对齐固件 Timer_Int_Enable_Read。
 */
bool ae103_timer_int_enable_read(const struct device *dev, uint8_t ch);

/**
 * @brief 读出某一路中断状态。
 * @return true = 有中断挂起（TIS 置位），false = 无中断。
 * @note 对齐固件 Timer_Int_Status。
 */
bool ae103_timer_int_status(const struct device *dev, uint8_t ch);

/**
 * @brief 清除某一路中断标志（读 TEOI 即清）。
 * @note 对齐固件 vDelayXms 里的 `TIMERx_TEOI;` 读操作。
 */
void ae103_timer_clear_irq(const struct device *dev, uint8_t ch);

#endif /* __RISCV_SPKSILICON_AE103_TIMER_H_ */
