/*
 * Copyright (c) 2026 Sparkle Silicon Technology Corp., Ltd.
 * SPDX-License-Identifier: Apache-2.0
 *
 * Nuclei N100 内核复位向量：__start。
 *
 * N100 为缩减面积将 mtvec 实现为只读寄存器（写被忽略），异常/中断入口由
 * mtvt(0x307) 向量表决定（向量表由芯片 ROM 复位后 scatterload 到 RAM
 * 0x30800，mtvt 固定指向）。
 *
 * 本文件是 spksilicon 家族内所有基于 N100 内核的 AE 系列
 * （AE101/AE102/AE103/AE201/AE203…）共享的内核公共层代码，由
 * CONFIG_NUCLEI_N100_SPECIAL 宏控切换：
 *   - N100 特化：不写 mtvec（写了也无害但误导），依赖 ROM 拷入的向量表。
 *   - 标准 RISC-V：写 mtvec 指向 zephyr 统一异常入口 _isr_wrapper，
 *     与 zephyr 通用 riscv-privileged/vector.S 行为一致。
 */

#include <zephyr/toolchain.h>

/* exports */
GTEXT(__start)

/* imports */
GTEXT(__reset)
GTEXT(_isr_wrapper)

SECTION_FUNC(vectors, __start)
#if defined(CONFIG_RISCV_GP)
	/* Initialize global pointer */
	.option push
	.option norelax
	la gp, __global_pointer$
	.option pop
#endif

	.option norvc;

#if defined(CONFIG_NUCLEI_N100_SPECIAL)
	/*
	 * N100 特化路径：mtvec 只读（写忽略）。trap 入口由 mtvt 向量表
	 * （0x30800，ROM scatterload）决定，此处不写 mtvec。
	 */
	#  la a0, (vector_base+0x80)//trap_base
	#  la a1, 0x30880
	#  lw t0, (a0)
	#  sw t0, (a1)
	 jal	ra,__scatterload_vector
#else
	/*
	 * 标准 RISC-V 路径：mtvec 指向 zephyr 统一异常入口 _isr_wrapper。
	 */
	la t0, _isr_wrapper
	csrw mtvec, t0
#endif /* CONFIG_NUCLEI_N100_SPECIAL */

	/* Jump to __reset */
	tail __reset
/*
 * _start：复位入口桩，落点 0x80084（FlashInfo.ReStart + FLASH_BASE_ADDR）。
 * 直接跳 zephyr __start（vector.S，链接在 0x80200）接管启动。
 */
 #define IVT_BASE_ADDR 0x30800
 #define IVT_BASE_SIZE 0x84
.globl __scatterload_vector		/* 全局符号 _start 供链接器和其他代码调度用*/
__scatterload_vector:
	la a0, vector_base
	la a1, IVT_BASE_ADDR
	la a2, (IVT_BASE_ADDR+IVT_BASE_SIZE)
1:
	lw t0, (a0)
	sw t0, (a1)
	addi a0, a0, 4
	addi a1, a1, 4
	bltu a1, a2, 1b
2:
    ret
