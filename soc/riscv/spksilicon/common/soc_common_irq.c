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

#if defined(CONFIG_NUCLEI_N100_SPECIAL)
void arch_irq_enable(unsigned int irq)
{
	// uint32_t irqcie;
	// irqcie = csr_read_set(0xBD1, 1 << irq);
	csr_set(0xBD1, 1 << irq);
}

void arch_irq_disable(unsigned int irq)
{
	// uint32_t irqcie;
	// irqcie = csr_read_clear(0xBD1, 1 << irq);
	csr_clear(0xBD1, 1 << irq);
}

int arch_irq_is_enabled(unsigned int irq)
{
	// uint32_t irqcie;
	// irqcie = csr_read(0xBD1);
	// return !!(irqcie & (1 << irq));
	return (((csr_read(0xBD1)) >> irq) & 1);
}

// void z_riscv_irq_priority_set(unsigned int irq, unsigned int prio, uint32_t flags)
// {
// 	// n100 not has function
// 	//level set
// }
#else

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

	if (level == 2)
	{
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

	if (level == 2)
	{
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

	if (level == 2)
	{
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

	if (level == 2)
	{
		riscv_plic_set_priority(irq, prio);
	}
}
#endif /* CONFIG_RISCV_HAS_PLIC */
#endif /* CONFIG_RISCV_HAS_CLIC */
#endif /* CONFIG_NUCLEI_N100_SPECIAL */

#if defined(CONFIG_RISCV_SOC_INTERRUPT_INIT)
/*kconfig:
select RISCV_SOC_INTERRUPT_INIT#要 SOC_AE103_NTO 被选中，这个选项就被强制打开，不可在 menuconfig
  里关掉。

config RISCV_SOC_INTERRUPT_INIT
	default y#设了默认值 y，理论上还能被覆盖成 n。
强制打开,即为CONFIG_RISCV_SOC_INTERRUPT_INIT宏,因此搜不到原宏,推荐打开	*/
#ifdef CONFIG_RISCV_SOC_HAS_CUSTOM_IRQ_LOCK_OPS
static ALWAYS_INLINE unsigned int z_soc_irq_lock(void)
{
	csr_set(mstatus, 0x8);//MSTATUS_MIE
}
#endif
__weak void soc_interrupt_init(void)
{
	/* ensure that all interrupts are disabled */
	// (void)arch_irq_lock();//startup.s已经做了,防止中断没注入的情况下跳转中断
#if defined(CONFIG_NUCLEI_N100_SPECIAL)//此处为非标准的IRQC
	csr_write(0xBD1, 0);//IRQCIE
	csr_write(0xBD0, 0);//IRQCIP
#else//此处为标准CLINT/PLIC
	// csr_write(mie, 0);
	// csr_write(mip, 0);
#endif
}
#endif
