/*
 * Copyright (c) 2026 Sparkle Silicon Technology Corp., Ltd.
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef __RISCV_SPKSILICON_AE103_TIMER_H_
#define __RISCV_SPKSILICON_AE103_TIMER_H_

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

#endif /* __RISCV_SPKSILICON_AE103_TIMER_H_ */
