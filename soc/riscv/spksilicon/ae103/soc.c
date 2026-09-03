/*
 * Copyright (c) 2026 Sparkle Silicon Technology Corp., Ltd.
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/arch/cpu.h>
#include <zephyr/irq.h>
#include <zephyr/sys/util.h>

#include <zephyr/arch/riscv/csr.h>

void sys_arch_reboot(int type)
{
	/* TODO: reset via SYSCTL (0x30400) reset register. */
	ARG_UNUSED(type);
}

#ifdef CONFIG_RISCV_SOC_INTERRUPT_INIT
void soc_interrupt_init(void)
{
	/*
	 * 基础：关全局中断并清空 mie/mip，与通用 riscv-privileged 的
	 * __weak soc_interrupt_init 行为保持一致。本文件提供强定义覆盖，
	 * 故需显式重复该逻辑，避免覆盖后丢失清中断步骤。
	 *
	 * ⚠️ N100 差异：手册 4.10 CSR 总表未列标准 mie(0x304)/mip(0x344)，
	 * 由 irqcie(0xBD1)/irqcip(0xBD0) 替代。下面 csrw mie/mip 展开为写
	 * 0x304/0x344，N100 上可能触发 illegal instruction，待实测确认后
	 * 改为 irqcie/irqcip（见 BUILD.md 7.9 关键结论 5）。
	 */
	(void)arch_irq_lock();
	csr_write(mie, 0);
	csr_write(mip, 0);

	/*
	 * TODO: init core CLIC and peripheral INTC0 (0x1000) / INTC1 (0x1400).
	 * N100 的 mtvt(0x307) 已由 ROM 固定指向 0x30800，向量表也由 ROM
	 * scatterload 完成，此处只需配置 CLIC 优先级/使能与 INTC 中断源。
	 */
}
#endif
