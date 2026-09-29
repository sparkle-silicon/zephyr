/*
 * Copyright (c) 2026 Sparkle Silicon Technology Corp., Ltd.
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/arch/cpu.h>
#include <zephyr/irq.h>
#include <zephyr/sys/util.h>

#include <zephyr/arch/riscv/csr.h>

#include "sysctl.h"

void sys_arch_reboot(int type)
{
	/* 系统复位落点：置 SYSCTL.RST1.CHIP_RST，复位生效后不返回。 */
	ae201_sysctl_system_reset();
	ARG_UNUSED(type);
}


/*
 * 无 systimer 平台的 k_uptime_ticks 兜底（AE201 专用，见 BUILD.md 7.15）
 *
 * 背景：kernel/CMakeLists.txt:109 用
 *         target_sources_ifdef(CONFIG_SYS_CLOCK_EXISTS kernel PRIVATE timeout.c timer.c)
 *       把整个 kernel/timeout.c 排除在编译之外；AE201 为 CONFIG_SYS_CLOCK_EXISTS=n，
 *       故 z_impl_k_uptime_ticks / sys_clock_tick_get 全族缺失。
 *
 * 触发：上游 subsys/logging/log_core.c 的 log_core_init() 用「运行时」判断选时间戳
 *       函数，两个分支都要编进目标文件：
 *         if (sys_clock_hw_cycles_per_sec() > 1000000)
 *                 log_set_timestamp_func(default_lf_get_timestamp, 1000U);
 *       AE201 为 20MHz > 1MHz，该分支存活 → default_lf_get_timestamp() →
 *       k_uptime_get_32() → k_uptime_ticks()，链接期 undefined reference。
 *       上游默认「所有平台都有 systimer」，无 tick 平台属边缘路径。
 *
 * 语义：无 tick 平台本就没有 uptime 概念，返回 0 而非伪造时间——日志时间戳恒 0
 *       是「无时间基准」的诚实反映。systimer 落地后 CONFIG_SYS_CLOCK_EXISTS=y，
 *       本实现被预处理器剔除，由 kernel/timeout.c 接管，无重复定义风险。
 */
#if !defined(CONFIG_SYS_CLOCK_EXISTS)
int64_t z_impl_k_uptime_ticks(void)
{
	return 0;
}
#endif
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
