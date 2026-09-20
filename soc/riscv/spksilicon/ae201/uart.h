/*
 * Copyright (c) 2026 Sparkle Silicon Technology Corp., Ltd.
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef __RISCV_SPKSILICON_AE201_UART_H_
#define __RISCV_SPKSILICON_AE201_UART_H_

#include <zephyr/device.h>

/* --- 寄存器偏移（16550 标准，8-bit 访问） ------------------------- */
#define AE201_UARTA_BASE_ADDR 0x5000UL
#define AE201_UARTB_BASE_ADDR 0x5400UL
#define AE201_UART0_BASE_ADDR 0x5800UL
#define AE201_UART1_BASE_ADDR 0x5C00UL

#define AE201_UART_REG_SIZE 8

#define AE201_UART_RBR_OFFSET 0x00
#define AE201_UART_THR_OFFSET 0x00
#define AE201_UART_DLL_OFFSET 0x00
#define AE201_UART_DLH_OFFSET 0x01
#define AE201_UART_IER_OFFSET 0x01
#define AE201_UART_IIR_OFFSET 0x02
#define AE201_UART_FCR_OFFSET 0x02
#define AE201_UART_LCR_OFFSET 0x03
#define AE201_UART_MICR_OFFSET 0x04
#define AE201_UART_LSR_OFFSET 0x05
#define AE201_UART_MSR_OFFSET 0x06
#define AE201_UART_USR_OFFSET 0x1F

/* LCR 位域 */
#define AE201_UART_LCR_DLAB BIT(7) /* 除数锁存访问使能 */
#define AE201_UART_LCR_8N1  0x03U  /* 8 数据位 / 无校验 / 1 停止位 */

/* LSR 位域 */
#define AE201_UART_LSR_DR   BIT(0) /* 接收数据就绪 */
#define AE201_UART_LSR_THRE BIT(5) /* 发送保持寄存器空 */

/* FCR 位域 */
#define AE201_UART_FCR_FIFOEN BIT(0)

#define AE201_UART0_CHANNEL 0
#define AE201_UART1_CHANNEL 1
#define AE201_UARTA_CHANNEL 2
#define AE201_UARTB_CHANNEL 3

struct ae201_uart_config{
	uintptr_t base;
	uint8_t oversample;  /* 过采样率：8 或 16 */
	uint32_t baudrate;   /* 初始波特率，DT current-speed */
};

/* --- 引脚复用（iomux）+ 时钟门控 -----------------------------------
 * 固件 UART 完整启动分两步（KERNEL_UART.c gpio2serial → serial_config）：
 *   1. gpio2serial：sysctl_iomux_uartx() 把引脚从 GPIO 复用为 UART 功能
 *                   + SYSCTL_MODEN0 使能 UART 时钟；
 *   2. serial_config：配波特率。
 * 本驱动原只有第 2 步，故串口引脚停在 GPIO 态、日志出不来。下表补齐第 1 步。
 * 引脚映射对齐固件 KERNEL_GPIO.c sysctl_iomux_uart*（GPIO 逻辑端口已换算为
 * SYSCTL PIO 编号：每 16 pin 一个 4 字节 CFG 块，pio = 端口号*2 + (pin≥16)）。
 */
struct ae201_uart_pin{
	uint32_t pio;   /* SYSCTL PIO 编号（PIO0~5） */
	uint32_t pin;   /* PIO 块内引脚 0~15 */
	uint32_t func;  /* 复用值 0~3（0=GPIO，见芯片手册） */
};

struct ae201_uart_iomux{
	uint32_t moden_mask;                 /* MODEN0 时钟使能位 */
	const struct ae201_uart_pin *pins;   /* TX/RX 引脚复用 */
	uint32_t pin_count;
};
/**
 * @brief 设置一路 AE201 UART 的波特率。
 *
 * 依据该路的过采样率（8x / 16x，见 binding 的 spksilicon,oversample-ratio）
 * 计算 divisor，并通过 DLAB 时序写入 DLL/DLH。
 *
 * @param dev  设备句柄（由 DT_INST 宏取得）。
 * @param baud 目标波特率。
 * @return 0 成功。
 */
int ae201_uart_baud_set(const struct device *dev, uint32_t baud);

#endif /* __RISCV_SPKSILICON_AE201_UART_H_ */
