/*
 * Copyright (c) 2017 Jean-Paul Etienne <fractalclone@gmail.com>
 * Copyright (c) 2026 Sparkle Silicon Technology Corp., Ltd.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief spksilicon N100 内核公共层中断管理代码。
 *
 * 原为 soc/riscv/common/riscv-privileged/soc_common_irq.c。spksilicon 各
 * AE 系列不再 select RISCV_PRIVILEGED（N100 内核 mtvec 只读，需自定义
 * vector.S），故把 arch_irq_enable/disable/is_enabled、
 * z_riscv_irq_priority_set 等符号本地化到内核公共层，供所有 AE 系列共享。
 *
 * N100 属蜂鸟 E203 血统、带 CLIC 向量中断控制器。⚠️ 手册 4.10 CSR 总表
 * 未列标准 mie(0x304)/mip(0x344)，由 irqcie(0xBD1)/irqcip(0xBD0) 替代；
 * 下面 #else 分支的 csr_read_set(mie,...) 等是通用 riscv-privileged 代码，
 * N100 上可能触发 illegal instruction。应尽快启用 CONFIG_RISCV_HAS_CLIC
 * 走 riscv_clic_* 分支，或改为 irqcie/irqcip（见 BUILD.md 7.9 关键结论 5）。
 */
#include <zephyr/irq.h>
#include <zephyr/irq_multilevel.h>

#include <zephyr/drivers/interrupt_controller/riscv_clic.h>
#include <zephyr/drivers/interrupt_controller/riscv_plic.h>

#if defined(CONFIG_RISCV_HAS_CLIC)

void arch_irq_enable(unsigned int irq)
{
	riscv_clic_irq_enable(irq);
}

void arch_irq_disable(unsigned int irq)
{
	riscv_clic_irq_disable(irq);
}

int arch_irq_is_enabled(unsigned int irq)
{
	return riscv_clic_irq_is_enabled(irq);
}

void z_riscv_irq_priority_set(unsigned int irq, unsigned int prio, uint32_t flags)
{
	riscv_clic_irq_priority_set(irq, prio, flags);
}

#else /* PLIC + HLINT/CLINT or HLINT/CLINT only */

void arch_irq_enable(unsigned int irq)
{
	uint32_t mie;

#if defined(CONFIG_RISCV_HAS_PLIC)
	unsigned int level = irq_get_level(irq);

	if (level == 2) {
		riscv_plic_irq_enable(irq);
		return;
	}
#endif

	/*
	 * CSR mie register is updated using atomic instruction csrrs
	 * (atomic read and set bits in CSR register)
	 */
	mie = csr_read_set(mie, 1 << irq);
}

void arch_irq_disable(unsigned int irq)
{
	uint32_t mie;

#if defined(CONFIG_RISCV_HAS_PLIC)
	unsigned int level = irq_get_level(irq);

	if (level == 2) {
		riscv_plic_irq_disable(irq);
		return;
	}
#endif

	/*
	 * Use atomic instruction csrrc to disable device interrupt in mie CSR.
	 * (atomic read and clear bits in CSR register)
	 */
	mie = csr_read_clear(mie, 1 << irq);
}

int arch_irq_is_enabled(unsigned int irq)
{
	uint32_t mie;

#if defined(CONFIG_RISCV_HAS_PLIC)
	unsigned int level = irq_get_level(irq);

	if (level == 2) {
		return riscv_plic_irq_is_enabled(irq);
	}
#endif

	mie = csr_read(mie);

	return !!(mie & (1 << irq));
}

#if defined(CONFIG_RISCV_HAS_PLIC)
void z_riscv_irq_priority_set(unsigned int irq, unsigned int prio, uint32_t flags)
{
	unsigned int level = irq_get_level(irq);

	if (level == 2) {
		riscv_plic_set_priority(irq, prio);
	}
}
#endif /* CONFIG_RISCV_HAS_PLIC */
#endif /* CONFIG_RISCV_HAS_CLIC */

#if defined(CONFIG_RISCV_SOC_INTERRUPT_INIT)
__weak void soc_interrupt_init(void)
{
	/* ensure that all interrupts are disabled */
	(void)arch_irq_lock();

	csr_write(mie, 0);
	csr_write(mip, 0);
}
#endif
