/*
 * Copyright (c) 2026 Sparkle Silicon Technology Corp., Ltd.
 * SPDX-License-Identifier: Apache-2.0
 *
 * @file uart.c
 * @brief AE103 UART 驱动 —— 16550 兼容，轮询版。
 *
 * AE103 的 UART 是标准 NS16550 寄存器布局（RBR/THR/DLL/IER/IIR/FCR/
 * LCR/MCR/LSR/MSR），外加一个非标扩展 USR@0x1F。与标准 16550 的唯一
 * 实质差异在波特率发生器：UART0~3 过采样 8x（非标），UARTA/B 过采样
 * 16x（标准），故 divisor 公式按路区分。
 *
 * 本驱动当前只做轮询（poll_in/poll_out），用于 console/printk。中断
 * 驱动（IER/IIR）需 AE103 INTC 落地后补，届时 binding 加 interrupts、
 * 驱动补 fifo_fill/irq_* 系列。
 *
 * 参考：drivers/serial/uart_ns16550.c（标准 16550 语义 + ITE 非标分频先例）。
 */

#define DT_DRV_COMPAT spksilicon_ae103_uart

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/sys_io.h>
#include <zephyr/sys/util.h>

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(ae103_uart, LOG_LEVEL_INF);

#include "uart.h"

/* --- 寄存器偏移（16550 标准，8-bit 访问） ------------------------- */
#define AE103_UART_RBR   0x00U /* 接收缓冲（读） */
#define AE103_UART_THR   0x00U /* 发送保持（写） */
#define AE103_UART_DLL   0x00U /* 除数低字节（DLAB=1） */
#define AE103_UART_DLH   0x01U /* 除数高字节（DLAB=1） */
#define AE103_UART_IER   0x01U /* 中断使能 */
#define AE103_UART_IIR   0x02U /* 中断状态 */
#define AE103_UART_FCR   0x02U /* FIFO 控制 */
#define AE103_UART_LCR   0x03U /* 线路控制 */
#define AE103_UART_MCR   0x04U /* 调制解调控制 */
#define AE103_UART_LSR   0x05U /* 线路状态 */
#define AE103_UART_MSR   0x06U /* 调制解调状态 */

/* LCR 位域 */
#define AE103_UART_LCR_DLAB BIT(7) /* 除数锁存访问使能 */
#define AE103_UART_LCR_8N1  0x03U  /* 8 数据位 / 无校验 / 1 停止位 */

/* LSR 位域 */
#define AE103_UART_LSR_DR   BIT(0) /* 接收数据就绪 */
#define AE103_UART_LSR_THRE BIT(5) /* 发送保持寄存器空 */

/* FCR 位域 */
#define AE103_UART_FCR_FIFOEN BIT(0)

struct ae103_uart_config {
	uintptr_t base;
	uint32_t clock_freq; /* 波特率时钟（Hz），DT clock-frequency */
	uint8_t oversample;  /* 过采样率：8 或 16 */
	uint32_t baudrate;   /* 初始波特率，DT current-speed */
};

static inline void uart_write8(const struct device *dev, uint8_t off, uint8_t val)
{
	const struct ae103_uart_config *cfg = dev->config;

	sys_write8(val, cfg->base + off);
}

static inline uint8_t uart_read8(const struct device *dev, uint8_t off)
{
	const struct ae103_uart_config *cfg = dev->config;

	return sys_read8(cfg->base + off);
}

/* ====================================================================
 * 波特率 divisor 计算 —— 复刻 ROM/KERNEL/KERNEL_UART.c UART_Init 的通式。
 *
 * 16550 波特率发生器：divisor = f_clock / (oversample * baud)。
 * AE103 的 oversample 因路而异（见 binding）：
 *   UART0/1：oversample = 8  （非标准，低功耗特制 8x 过采样）
 *   UARTA/B：oversample = 16 （16550 标准 16x 过采样）
 *
 * ROM 原文用 channel 编码推导（chanel>>1：0=UART0/1, 1=UARTA/B）：
 *   divisor = (freq / baud + (0b100 << (chanel>>1))) >> (3 + (chanel>>1));
 * 把 oversample 当变量即合并为一条通式：
 *   half    = oversample / 2          // 四舍五入的半个 ulp（8x→4, 16x→8）
 *   divisor = (freq / baud + half) / oversample
 *           = (freq + half*baud) / (oversample*baud)，与 ns16550 标准写法同构。
 * ==================================================================== */
static uint32_t ae103_uart_baud_divisor(const struct ae103_uart_config *cfg,
					uint32_t baud)
{
	uint32_t half = cfg->oversample / 2U;
	uint32_t divisor = (cfg->clock_freq / baud + half) / cfg->oversample;

	/* divisor 必须 ≥ 1：为 0 会关停波特率发生器（DLL/DLH 写 0）。 */
	return divisor < 1U ? 1U : divisor;
}

int ae103_uart_baud_set(const struct device *dev, uint32_t baud)
{
	const struct ae103_uart_config *cfg = dev->config;
	uint32_t divisor = ae103_uart_baud_divisor(cfg, baud);

	/* DLAB 时序：置 DLAB → 写 DLL/DLH → 清 DLAB。 */
	uart_write8(dev, AE103_UART_LCR, AE103_UART_LCR_DLAB | AE103_UART_LCR_8N1);
	uart_write8(dev, AE103_UART_DLL, (uint8_t)(divisor & 0xFFU));
	uart_write8(dev, AE103_UART_DLH, (uint8_t)((divisor >> 8) & 0xFFU));
	uart_write8(dev, AE103_UART_LCR, AE103_UART_LCR_8N1);

	return 0;
}

static int ae103_uart_init(const struct device *dev)
{
	const struct ae103_uart_config *cfg = dev->config;

	/* 波特率 + 8N1。 */
	ae103_uart_baud_set(dev, cfg->baudrate);

	/* FIFO 使能（轮询下不影响收发正确性，但对齐裸机固件 serial_config）。 */
	uart_write8(dev, AE103_UART_FCR, AE103_UART_FCR_FIFOEN);

	LOG_INF("AE103 UART @ 0x%lx oversample %ux ready",
		(unsigned long)cfg->base, cfg->oversample);

	return 0;
}

static int ae103_uart_poll_in(const struct device *dev, unsigned char *c)
{
	if ((uart_read8(dev, AE103_UART_LSR) & AE103_UART_LSR_DR) != 0U) {
		*c = uart_read8(dev, AE103_UART_RBR);
		return 0;
	}

	return -1;
}

static void ae103_uart_poll_out(const struct device *dev, unsigned char c)
{
	/* 等发送保持寄存器空（THRE=1）。 */
	while ((uart_read8(dev, AE103_UART_LSR) & AE103_UART_LSR_THRE) == 0U) {
	}

	uart_write8(dev, AE103_UART_THR, c);
}

static const struct uart_driver_api ae103_uart_api = {
	.poll_in = ae103_uart_poll_in,
	.poll_out = ae103_uart_poll_out,
};

#define AE103_UART_INIT(n)                                             \
	static const struct ae103_uart_config uart_config_##n = {      \
		.base = DT_INST_REG_ADDR(n),                            \
		.clock_freq = DT_INST_PROP(n, clock_frequency),          \
		.oversample = DT_INST_PROP(n, spksilicon_oversample_ratio), \
		.baudrate = DT_INST_PROP_OR(n, current_speed, 115200),   \
	};                                                               \
	DEVICE_DT_INST_DEFINE(n, ae103_uart_init, NULL, NULL,           \
			      &uart_config_##n, PRE_KERNEL_1,             \
			      CONFIG_SERIAL_INIT_PRIORITY, &ae103_uart_api);

DT_INST_FOREACH_STATUS_OKAY(AE103_UART_INIT)
