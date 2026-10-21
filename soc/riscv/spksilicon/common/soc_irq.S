/*
 * Copyright (c) 2017 Jean-Paul Etienne <fractalclone@gmail.com>
 * Copyright (c) 2026 Sparkle Silicon Technology Corp., Ltd.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * spksilicon N100 内核公共层中断管理汇编。
 *
 * 原为 soc/riscv/common/riscv-privileged/soc_irq.S。spksilicon 各 AE 系列
 * 不再 select RISCV_PRIVILEGED（N100 内核 mtvec 只读，需自定义 vector.S），
 * 故把 __soc_handle_irq 本地化到内核公共层，供所有 AE 系列共享，避免缺符号。
 */
#include <zephyr/kernel_structs.h>
#include <offsets.h>
#include <zephyr/toolchain.h>
#include <zephyr/linker/sections.h>
#include <zephyr/arch/riscv/irq.h>

/*
 * __soc_handle_irq 定义为 .weak，允许不完全遵循 riscv 特权规范
 * 的 SoC 自行重实现。
 */
WTEXT(__soc_handle_irq)

/*
 * 内核特有函数：处理产生中断的 pending IRQ 号。
 * 异常号通过寄存器 a0 传入。
 */
SECTION_FUNC(exception.other, __soc_handle_irq)
#if defined(CONFIG_NUCLEI_N100_SPECIAL)
	/*
	 * N100（E203 血统）没有标准 mie(0x304)/mip(0x344)，对应物是
	 * irqcie(0xBD1)/irqcip(0xBD0)。标准路径的 `csrrc mip` 在 N100 上会
	 * 触发 illegal instruction（mcause=2），而 mcause 已经在 arch isr.S
	 * 里解析过，此处再执行就会在每次中断时把内核打死。
	 *
	 * irqcip 的 pending 位由中断源本身驱动：
	 *   - bit1 计时器：ISR 里重写 mtimecmp 后自然撤销；
	 *   - 外部源（bit3 起 / bit30-31）：由 INTC 驱动读寄存器后撤销。
	 * 因此这里只需回写本 IRQ 对应的那一位（写 1 清除，写 0 无影响），
	 * 对硬连线驱动的位写操作被硬件忽略，无害；对软件可清位则即时撤销。
	 *
	 * ⚠️ a0 必须原样返回：arch/riscv/core/isr.S 用 a0 索引
	 *    _sw_isr_table，改动它会导致跳到错误 ISR。
	 */
	li	t0, 31
	bgtu	a0, t0, 1f		/* 越界（无此中断源）不清位 */
	li	t0, 1
	sll	t0, t0, a0
	csrw	0xBD0, t0		/* N100_CSR_IRQCIP */
1:
#else
	/* Clear exception number from CSR mip register */
	li t1, 1
	sll t0, t1, a0
	csrrc t1, mip, t0
#endif /* CONFIG_NUCLEI_N100_SPECIAL */

	/* Return */
	ret
