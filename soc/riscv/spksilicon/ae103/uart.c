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
#include "clock.h"   /* ae103_clock_freq_get（波特率时钟动态拉取） */
#include "sysctl.h"  /* ae103_sysctl_pio_cfg_set / clock_enable（引脚复用 + 时钟门控） */

static const struct ae103_uart_pin uart0_pins[] = {
	{ 1U, 8U, 2U },   /* GPIOA24，UART0 TX */
	{ 1U, 9U, 2U },   /* GPIOA25，UART0 RX */
};

static const struct ae103_uart_pin uart1_pins[] = {
	{ 2U, 1U, 1U },   /* GPIOB1，UART1 TX */
	{ 2U, 3U, 1U },   /* GPIOB3，UART1 RX */
};

static const struct ae103_uart_pin uarta_pins[] = {
	/* ⚠️ 固件 sysctl_iomux_uarta 仅配 GPIOB6(func=3)，其 disable 却清
	 * GPIOA8/9（历史遗留），资料不一致，UARTA 实际引脚待芯片手册确认。 */
	{ 2U, 6U, 3U },   /* GPIOB6，UARTA（单引脚，疑点） */
};

static const struct ae103_uart_pin uartb_pins[] = {
	{ 3U, 9U, 2U },   /* GPIOB25，UARTB TX */
	{ 3U, 10U, 2U },  /* GPIOB26，UARTB RX */
};

enum ae103_uart_channel{
	AE103_UART_CH_UARTA,
	AE103_UART_CH_UARTB,
	AE103_UART_CH_UART0,
	AE103_UART_CH_UART1,
};

static const struct ae103_uart_iomux uart_iomux[] = {
	[AE103_UART_CH_UARTA] = { AE103_SYSCTL_MODEN0_UARTA_EN, uarta_pins, ARRAY_SIZE(uarta_pins) },
	[AE103_UART_CH_UARTB] = { AE103_SYSCTL_MODEN0_UARTB_EN, uartb_pins, ARRAY_SIZE(uartb_pins) },
	[AE103_UART_CH_UART0] = { AE103_SYSCTL_MODEN0_UART0_EN, uart0_pins, ARRAY_SIZE(uart0_pins) },
	[AE103_UART_CH_UART1] = { AE103_SYSCTL_MODEN0_UART1_EN, uart1_pins, ARRAY_SIZE(uart1_pins) },
};

/* base 地址 → 通道号（4 路基址芯片固定） */
static int uart_channel(uintptr_t base)
{
	switch (base)
	{
		case AE103_UARTA_BASE_ADDR: return AE103_UART_CH_UARTA;
		case AE103_UARTB_BASE_ADDR: return AE103_UART_CH_UARTB;
		case AE103_UART0_BASE_ADDR: return AE103_UART_CH_UART0;
		case AE103_UART1_BASE_ADDR: return AE103_UART_CH_UART1;
		default: return -1;
	}
}

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
	uint32_t freq = ae103_clock_freq_get(AE103_CLOCK_DOMAIN_UART);
	uint32_t divisor = (freq / baud + half) / cfg->oversample;

	/* divisor 必须 ≥ 1：为 0 会关停波特率发生器（DLL/DLH 写 0）。 */
	return divisor < 1U ? 1U : divisor;
}

int ae103_uart_baud_set(const struct device *dev, uint32_t baud)
{
	const struct ae103_uart_config *cfg = dev->config;
	uint32_t divisor = ae103_uart_baud_divisor(cfg, baud);

	/* DLAB 时序：置 DLAB → 写 DLL/DLH → 清 DLAB。 */
	uart_write8(dev, AE103_UART_LCR_OFFSET,
		    AE103_UART_LCR_DLAB | AE103_UART_LCR_8N1);
	uart_write8(dev, AE103_UART_DLL_OFFSET, (uint8_t)(divisor & 0xFFU));
	uart_write8(dev, AE103_UART_DLH_OFFSET, (uint8_t)((divisor >> 8) & 0xFFU));
	uart_write8(dev, AE103_UART_LCR_OFFSET, AE103_UART_LCR_8N1);

	return 0;
}

static int ae103_uart_init(const struct device *dev)
{
	const struct ae103_uart_config *cfg = dev->config;
	int ch = uart_channel(cfg->base);

	/* 第 1 步：引脚复用为 UART + 使能时钟（对齐固件 gpio2serial）。 */
	if (ch >= 0)
	{
		const struct ae103_uart_iomux *mux = &uart_iomux[ch];

		for (uint32_t i = 0U; i < mux->pin_count; i++)
		{
			ae103_sysctl_pio_cfg_set(mux->pins[i].pio,
						 mux->pins[i].pin,
						 mux->pins[i].func);
		}
		ae103_sysctl_clock_enable(mux->moden_mask, 0U);
	}

	/* 第 2 步：波特率 + 8N1（对齐固件 serial_config）。 */
	ae103_uart_baud_set(dev, cfg->baudrate);

	/* FIFO 使能（轮询下不影响收发正确性，但对齐裸机固件 serial_config）。 */
	uart_write8(dev, AE103_UART_FCR_OFFSET, AE103_UART_FCR_FIFOEN);

	LOG_INF("AE103 UART @ 0x%lx oversample %ux ready",
		(unsigned long)cfg->base, cfg->oversample);

	return 0;
}

static int ae103_uart_poll_in(const struct device *dev, unsigned char *c)
{
	if ((uart_read8(dev, AE103_UART_LSR_OFFSET) & AE103_UART_LSR_DR) != 0U)
	{
		*c = uart_read8(dev, AE103_UART_RBR_OFFSET);
		return 0;
	}

	return -1;
}

static void ae103_uart_poll_out(const struct device *dev, unsigned char c)
{
	/* 等发送保持寄存器空（THRE=1）。 */
	while ((uart_read8(dev, AE103_UART_LSR_OFFSET) & AE103_UART_LSR_THRE) == 0U)
	{
	}

	uart_write8(dev, AE103_UART_THR_OFFSET, c);
}

static const struct uart_driver_api ae103_uart_api = {
	.poll_in = ae103_uart_poll_in,
	.poll_out = ae103_uart_poll_out,
};

#define AE103_UART_INIT(n)                                             \
	static const struct ae103_uart_config uart_config_##n = {      \
		.base = DT_INST_REG_ADDR(n),                            \
		.oversample = DT_INST_PROP(n, spksilicon_oversample_ratio), \
		.baudrate = DT_INST_PROP_OR(n, current_speed, 115200),   \
	};                                                               \
	DEVICE_DT_INST_DEFINE(n, ae103_uart_init, NULL, NULL,           \
			      &uart_config_##n, PRE_KERNEL_1,             \
			      CONFIG_SERIAL_INIT_PRIORITY, &ae103_uart_api);

DT_INST_FOREACH_STATUS_OKAY(AE103_UART_INIT)
