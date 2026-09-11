/*
 * Copyright (c) 2026 Sparkle Silicon Technology Corp., Ltd.
 * SPDX-License-Identifier: Apache-2.0
 *
 * @file sysctl.c
 * @brief AE103 SYSCTL 驱动实现（时钟门控 / 复位 / 分频 / 引脚复用）。
 *
 * 命名与寄存器布局见 sysctl.h 文件头。实现要点：
 *   - 纯 MMIO 读写，无 LOG、无全局变量（wdt.c 早期 _WdogInit 路径安全）。
 *   - 门控/复位均为「读-改-写」，避免误清其他模块位。
 *   - 复位时序对齐固件 sysctl_mod_reset_start/finish：置位 → 清位，无额外延时。
 */

#include <zephyr/arch/riscv/sys_io.h>   /* sys_read32/sys_write32/sys_read8 架构实现 */

#include "sysctl.h"

/* ================= 寄存器访问原语 ================================== */
static inline uint32_t sysctl_read32(uint32_t off)
{
	return sys_read32(AE103_SYSCTL_BASE_ADDR + off);
}

static inline void sysctl_write32(uint32_t off, uint32_t val)
{
	sys_write32(val, AE103_SYSCTL_BASE_ADDR + off);
}

/* ================= 时钟门控 ======================================== */
void ae103_sysctl_clock_enable(uint32_t moden0_mask, uint32_t moden1_mask)
{
	if (moden0_mask != 0U) {
		sysctl_write32(AE103_SYSCTL_MODEN0_OFFSET,
			       sysctl_read32(AE103_SYSCTL_MODEN0_OFFSET) | moden0_mask);
	}
	if (moden1_mask != 0U) {
		sysctl_write32(AE103_SYSCTL_MODEN1_OFFSET,
			       sysctl_read32(AE103_SYSCTL_MODEN1_OFFSET) | moden1_mask);
	}
}

void ae103_sysctl_clock_disable(uint32_t moden0_mask, uint32_t moden1_mask)
{
	if (moden0_mask != 0U) {
		sysctl_write32(AE103_SYSCTL_MODEN0_OFFSET,
			       sysctl_read32(AE103_SYSCTL_MODEN0_OFFSET) & ~moden0_mask);
	}
	if (moden1_mask != 0U) {
		sysctl_write32(AE103_SYSCTL_MODEN1_OFFSET,
			       sysctl_read32(AE103_SYSCTL_MODEN1_OFFSET) & ~moden1_mask);
	}
}

/* ================= 外设复位 ======================================== */
void ae103_sysctl_reset_start(uint32_t rst0_mask, uint32_t rst1_mask)
{
	if (rst0_mask != 0U) {
		sysctl_write32(AE103_SYSCTL_RST0_OFFSET,
			       sysctl_read32(AE103_SYSCTL_RST0_OFFSET) | rst0_mask);
	}
	if (rst1_mask != 0U) {
		sysctl_write32(AE103_SYSCTL_RST1_OFFSET,
			       sysctl_read32(AE103_SYSCTL_RST1_OFFSET) | rst1_mask);
	}
}

void ae103_sysctl_reset_finish(uint32_t rst0_mask, uint32_t rst1_mask)
{
	if (rst0_mask != 0U) {
		sysctl_write32(AE103_SYSCTL_RST0_OFFSET,
			       sysctl_read32(AE103_SYSCTL_RST0_OFFSET) & ~rst0_mask);
	}
	if (rst1_mask != 0U) {
		sysctl_write32(AE103_SYSCTL_RST1_OFFSET,
			       sysctl_read32(AE103_SYSCTL_RST1_OFFSET) & ~rst1_mask);
	}
}

void ae103_sysctl_periph_reset(uint32_t rst0_mask, uint32_t rst1_mask)
{
	ae103_sysctl_reset_start(rst0_mask, rst1_mask);
	/* 复位脉冲需维持若干周期，空转几次保证硬件采样到复位沿。 */
	__asm__ volatile("nop\n\tnop\n\tnop\n\tnop");
	ae103_sysctl_reset_finish(rst0_mask, rst1_mask);
}

/* ================= 系统复位 ======================================== */
void ae103_sysctl_system_reset(void)
{
	sysctl_write32(AE103_SYSCTL_RST1_OFFSET,
		       sysctl_read32(AE103_SYSCTL_RST1_OFFSET) | AE103_SYSCTL_RST1_CHIP_RST);

	/* 复位生效后不会返回；兜底防复位未生效时 CPU 继续跑。 */
	for (;;) {
	}
}

/* ================= 时钟分频 ======================================== */
void ae103_sysctl_clock_div_set(uint32_t clkdiv_offset, uint8_t div)
{
	sysctl_write32(clkdiv_offset, (uint32_t)div);
}

uint8_t ae103_sysctl_clock_div_get(uint32_t clkdiv_offset)
{
	return (uint8_t)(sysctl_read32(clkdiv_offset) & 0xFFU);
}

/* ================= 引脚复用 ======================================== */
void ae103_sysctl_pio_cfg_set(uint32_t pio, uint32_t pin, uint32_t func)
{
	uint32_t addr = AE103_SYSCTL_PIO0_CFG_OFFSET + (pio << 2);
	uint32_t cfg = sysctl_read32(addr);

	cfg &= ~(3U << (pin << 1));
	cfg |= (func & 3U) << (pin << 1);
	sysctl_write32(addr, cfg);
}
