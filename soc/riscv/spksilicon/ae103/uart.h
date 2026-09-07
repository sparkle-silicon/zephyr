/*
 * Copyright (c) 2026 Sparkle Silicon Technology Corp., Ltd.
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef __RISCV_SPKSILICON_AE103_UART_H_
#define __RISCV_SPKSILICON_AE103_UART_H_

#include <zephyr/device.h>

/**
 * @brief 设置一路 AE103 UART 的波特率。
 *
 * 依据该路的过采样率（8x / 16x，见 binding 的 spksilicon,oversample-ratio）
 * 计算 divisor，并通过 DLAB 时序写入 DLL/DLH。
 *
 * @param dev  设备句柄（由 DT_INST 宏取得）。
 * @param baud 目标波特率。
 * @return 0 成功。
 */
int ae103_uart_baud_set(const struct device *dev, uint32_t baud);

#endif /* __RISCV_SPKSILICON_AE103_UART_H_ */
