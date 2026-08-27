/*
 * Copyright (c) 2026 Sparkle Silicon Technology Corp., Ltd.
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/arch/cpu.h>
#include <zephyr/sys/util.h>

void sys_arch_reboot(int type)
{
	/* TODO: reset via SYSCTL (0x30400) reset register. */
	ARG_UNUSED(type);
}

#ifdef CONFIG_RISCV_SOC_INTERRUPT_INIT
void soc_interrupt_init(void)
{
	/* TODO: init core CLIC and peripheral INTC0 (0x1000) / INTC1 (0x1400). */
}
#endif
