/*
 * Copyright (c) 2026 Sparkle Silicon Technology Corp., Ltd.
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/arch/cpu.h>
#include <zephyr/irq.h>
#include <zephyr/sys/util.h>

#include <zephyr/arch/riscv/csr.h>

#include "ae201_sysctl_sl.h"

void sys_arch_reboot(int type)
{
	/* 系统复位落点：置 SYSCTL.RST1.CHIP_RST，复位生效后不返回。 */
	ae201_sysctl_sl_system_reset();
	ARG_UNUSED(type);
}


/*
 * 无 systimer 平台的历史兜底已移除（2026-10-19）。
 *
 * 曾经这里提供 __weak sys_clock_cycle_get_32() 与 z_impl_k_uptime_ticks()，
 * 目的只是让 CONFIG_SYS_CLOCK_EXISTS=n 时的日志子系统能链接（当时还没有
 * systimer，k_uptime 无真实时间基准）。systimer 落地后两个符号都有正式实现：
 *
 *   sys_clock_cycle_get_32()  → 父仓库 platform/ae201_systimer.c（mcycle）
 *   z_impl_k_uptime_ticks()   → kernel/timeout.c（sys_clock_announce 累加）
 *
 * ⚠️ 原实现是 __weak：留着不会报重复符号，但会**遮蔽**真实实现 ——
 *    必须整块删除，而不是仅在开启开关时关掉。
 */
#if defined(CONFIG_RISCV_SOC_INTERRUPT_INIT)
#define AE201_INTC0_BASE_ADDR        0x1000UL
#define AE201_INTC1_BASE_ADDR        0x1400UL
#define AE201_ICTL0_BASE_ADDR        AE201_INTC0_BASE_ADDR
#define AE201_ICTL1_BASE_ADDR        AE201_INTC1_BASE_ADDR
#define AE201_INTC_OFFSET_MASK      0x3FF
#define AE201_ICTL_OFFSET_MASK      AE201_INTC_OFFSET_MASK

#if 1/* 采用 mask 的格式 */
#define INTC0_REG_ADDR(offset)        	((AE201_INTC0_BASE_ADDR) + ((offset)&AE201_INTC_OFFSET_MASK))
#define INTC1_REG_ADDR(offset)        	((AE201_INTC1_BASE_ADDR) + ((offset)&AE201_INTC_OFFSET_MASK))
#else
#define INTC0_REG_ADDR(offset)        	((AE201_INTC0_BASE_ADDR) + (offset))
#define INTC1_REG_ADDR(offset)        	((AE201_INTC1_BASE_ADDR) + (offset))
#endif
#define ICTL0_REG_ADDR(offset) 		INTC0_REG_ADDR(offset)
#define ICTL1_REG_ADDR(offset) 		INTC1_REG_ADDR(offset)

static ALWAYS_INLINE uint8_t intc0_read8(mem_addr_t offset)
{
	return sys_read8(INTC0_REG_ADDR(offset));
}
static ALWAYS_INLINE uint8_t intc1_read8(mem_addr_t offset)
{
	return sys_read8(INTC1_REG_ADDR(offset));
}
static ALWAYS_INLINE void intc0_write8(mem_addr_t offset, uint8_t val)
{
	sys_write8((uint8_t)(val), INTC0_REG_ADDR(offset));
}
static ALWAYS_INLINE void intc1_write8(mem_addr_t offset, uint8_t val)
{
	sys_write8((uint8_t)(val), INTC1_REG_ADDR(offset));
}
static ALWAYS_INLINE uint8_t intcm_read8(uint8_t m, mem_addr_t offset)
{
	if (m == 0)
		return intc0_read8(offset);
	else if (m == 1)
		return intc1_read8(offset);
	else
		return 0;
}
static ALWAYS_INLINE void intcm_write8(uint8_t m, mem_addr_t offset, uint8_t val)
{
	if (m == 0)
		intc0_write8(offset, val);
	else if (m == 1)
		intc1_write8(offset, val);
}
#define ICTL_INTEN0_OFFSET	0
#define ICTL_INTEN1_OFFSET	1
#define ICTL_INTEN2_OFFSET	2
#define ICTL_INTEN3_OFFSET	3
#define ICTL_INTEN4_OFFSET	4
#define ICTL_INTEN5_OFFSET	5
#define ICTL_INTEN6_OFFSET	6
#define ICTL_INTEN7_OFFSET	7
#define ICTL_INTEN_MAX_OFFSET	8

void soc_interrupt_init(void)
{
	{//default(CPU INit)
		/* ensure that all interrupts are disabled */
		(void)arch_irq_lock();// csr_clear(mstatus, 0x00000008);

		csr_write(0xBD1, 0);/*irqcie*/ // csr_write(mie, 0);
		csr_write(0xBD0, 0);/*irqcip*/ // csr_write(mip, 0);
	}

	{//中断控制器(INTC)

		/* Ensure interrupts of soc are disabled at default */
		for (uint32_t m = 0; m < 2; m++)
		{
			for (int n = ICTL_INTEN0_OFFSET; n < ICTL_INTEN_MAX_OFFSET; n++)
			{
				intcm_write8(m, n, 0);
			}

		}
		/* Enable M-mode external interrupt */
		csr_set(0xBD1, (1 << 30) | (1 << 31));//csr_set(mie, MIP_MEIP);

	}

}
#endif
