/*
 * Copyright (c) 2026 Sparkle Silicon Technology Corp., Ltd.
 * SPDX-License-Identifier: Apache-2.0
 *
 * @file timer.c
 * @brief AE103 硬件定时器驱动 —— 外设驱动移植模板（REFERENCE DRIVER）。
 *
 * 这是 AE103 SoC 的第一个外设驱动，也是后续所有外设驱动（INTC / UART /
 * GPIO ...）的移植模板。它完整演示了 Zephyr 外设驱动的标准流程：
 *
 *   1. DT binding   dts/bindings/timer/spksilicon,ae103-timer.yaml
 *   2. DT node      dts/riscv/spksilicon/ae103.dtsi        (timer@1800)
 *   3. Kconfig      CONFIG_AE103_TIMER (soc/.../ae103/Kconfig.soc)
 *   4. 驱动本体     本文件 (DEVICE_DT_INST_DEFINE + API)
 *   5. 编译注册      soc/riscv/spksilicon/ae103/CMakeLists.txt
 *   6. 使能         boards/riscv/ae103_nto/ae103_nto_defconfig
 *
 * 硬件：4 路 16-bit 递减计数器 @ 0x1800，每路 stride 0x14。
 * 注：本驱动当前只做基础读写（不含中断）。中断需 INTC 驱动落地后，
 *     再补 TCR bit2 / TIS / TEOI 相关逻辑。
 */

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/sys_io.h>

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(ae103_timer, LOG_LEVEL_INF);

#include "timer.h"

#define DT_DRV_COMPAT spksilicon_ae103_timer

/* --- 寄存器布局（见 binding 注释） --------------------------------- */
#define AE103_TIMER_TLC         0x00U /* load count (16-bit, LE) */
#define AE103_TIMER_TCV         0x04U /* current count (16-bit, RO) */
#define AE103_TIMER_TCR         0x08U /* control */
#define AE103_TIMER_TEOI        0x0CU /* end-of-interrupt clear */
#define AE103_TIMER_TIS         0x10U /* interrupt status */
#define AE103_TIMER_CH_STRIDE   0x14U /* 每路偏移 */
#define AE103_TIMER_CH_COUNT    4U

/* TCR 位域 */
#define AE103_TIMER_TCR_EN      BIT(0) /* 1 = 运行 */
#define AE103_TIMER_TCR_LOOP    BIT(1) /* 1 = 自动重装载 */
#define AE103_TIMER_TCR_INT_MSK BIT(2) /* 1 = 屏蔽中断 */

/* TIS 位域 */
#define AE103_TIMER_TIS_INT     BIT(0) /* 1 = 中断挂起 */

struct ae103_timer_config {
	uintptr_t base;
};

static inline uintptr_t timer_ch_base(const struct device *dev, uint8_t ch)
{
	const struct ae103_timer_config *cfg = dev->config;

	return cfg->base + ((uintptr_t)ch * AE103_TIMER_CH_STRIDE);
}

/*
 * 16-bit 的 load/current 寄存器由两个 8-bit 寄存器组成（小端）：
 * 低字节在 +0，高字节在 +1。硬件按 8-bit 访问，故拆成两次读写。
 */
static inline uint16_t timer_read16(uintptr_t base, uint8_t off)
{
	return (uint16_t)(sys_read8(base + off) |
			  (sys_read8(base + off + 1U) << 8));
}

static inline void timer_write16(uintptr_t base, uint8_t off, uint16_t val)
{
	sys_write8((uint8_t)(val & 0xFFU), base + off);
	sys_write8((uint8_t)((val >> 8) & 0xFFU), base + off + 1U);
}

static int ae103_timer_init(const struct device *dev)
{
	const struct ae103_timer_config *cfg = dev->config;

	/* 上电自检：停止全部 4 路，确认寄存器区可访问。 */
	for (uint8_t ch = 0U; ch < AE103_TIMER_CH_COUNT; ch++) {
		sys_write8(0U, timer_ch_base(dev, ch) + AE103_TIMER_TCR);
	}

	LOG_INF("AE103 timer @ 0x%lx ready (%u channels)",
		(unsigned long)cfg->base, AE103_TIMER_CH_COUNT);

	return 0;
}

/* --- 对外 API ------------------------------------------------------ */

void ae103_timer_start(const struct device *dev, uint8_t ch, uint16_t load, bool loop)
{
	uintptr_t base = timer_ch_base(dev, ch);
	uint8_t tcr = AE103_TIMER_TCR_EN;

	if (loop) {
		tcr |= AE103_TIMER_TCR_LOOP;
	}

	timer_write16(base, AE103_TIMER_TLC, load);
	sys_write8(tcr, base + AE103_TIMER_TCR);
}

void ae103_timer_stop(const struct device *dev, uint8_t ch)
{
	sys_write8(0U, timer_ch_base(dev, ch) + AE103_TIMER_TCR);
}

uint16_t ae103_timer_get_count(const struct device *dev, uint8_t ch)
{
	return timer_read16(timer_ch_base(dev, ch), AE103_TIMER_TCV);
}

/* --- 使能/禁用（对齐固件 TIMER_Enable / TIMER_Disable） ----------- */
void ae103_timer_enable(const struct device *dev, uint8_t ch)
{
	uintptr_t tcr = timer_ch_base(dev, ch) + AE103_TIMER_TCR;

	sys_write8(sys_read8(tcr) | AE103_TIMER_TCR_EN, tcr);
}

void ae103_timer_disable(const struct device *dev, uint8_t ch)
{
	uintptr_t tcr = timer_ch_base(dev, ch) + AE103_TIMER_TCR;

	sys_write8(sys_read8(tcr) & ~AE103_TIMER_TCR_EN, tcr);
}

/* --- 中断控制（对齐固件 Timer_Int_* 系列） ------------------------- */
void ae103_timer_int_enable(const struct device *dev, uint8_t ch)
{
	uintptr_t tcr = timer_ch_base(dev, ch) + AE103_TIMER_TCR;

	/* INT_MSK = 0 → 允许中断 */
	sys_write8(sys_read8(tcr) & ~AE103_TIMER_TCR_INT_MSK, tcr);
}

void ae103_timer_int_disable(const struct device *dev, uint8_t ch)
{
	uintptr_t tcr = timer_ch_base(dev, ch) + AE103_TIMER_TCR;

	sys_write8(sys_read8(tcr) | AE103_TIMER_TCR_INT_MSK, tcr);
}

bool ae103_timer_int_enable_read(const struct device *dev, uint8_t ch)
{
	uintptr_t tcr = timer_ch_base(dev, ch) + AE103_TIMER_TCR;

	return (sys_read8(tcr) & AE103_TIMER_TCR_INT_MSK) == 0U;
}

bool ae103_timer_int_status(const struct device *dev, uint8_t ch)
{
	uintptr_t tis = timer_ch_base(dev, ch) + AE103_TIMER_TIS;

	return (sys_read8(tis) & AE103_TIMER_TIS_INT) != 0U;
}

void ae103_timer_clear_irq(const struct device *dev, uint8_t ch)
{
	/* 读 TEOI 即清中断（对齐固件 vDelayXms 的 `TIMERx_TEOI;`） */
	sys_read8(timer_ch_base(dev, ch) + AE103_TIMER_TEOI);
}

/* --- 设备注册 ------------------------------------------------------ */
#define AE103_TIMER_INIT(n)                                             \
	static const struct ae103_timer_config timer_config_##n = {      \
		.base = DT_INST_REG_ADDR(n),                            \
	};                                                               \
	DEVICE_DT_INST_DEFINE(n, ae103_timer_init, NULL, NULL,         \
			      &timer_config_##n, POST_KERNEL,          \
			      CONFIG_AE103_TIMER_INIT_PRIORITY, NULL);

DT_INST_FOREACH_STATUS_OKAY(AE103_TIMER_INIT)
