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
	/* Clear exception number from CSR mip register */
	li t1, 1
	sll t0, t1, a0
	csrrc t1, mip, t0

	/* Return */
	ret
