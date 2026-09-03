/*
 * Copyright (c) 2026 Sparkle Silicon Technology Corp., Ltd.
 * SPDX-License-Identifier: Apache-2.0
 *
 * Nuclei N100 内核特化 CSR 集中管理 + 与标准 RISC-V 的差异标注。
 *
 * 本文件是 spksilicon 家族内所有基于 N100 内核的 AE 系列
 * （AE101/AE102/AE103/AE201/AE203…）共享的内核公共层头文件，由
 * CONFIG_NUCLEI_N100_SPECIAL 宏控，避免散落各处；不通过 build 命令
 * 外部注入任何宏（见 build.sh / Kconfig）。
 *
 * 来源：ecfw-zephyr/misc/startup.H 的 CSR 定义 + SPK32A20X用户手册_v0.5.9
 * 4.10 节 CSR 总表（表 4-3，权威清单）。芯片仍在研发中，部分寄存器语义
 * 待验证，已用 [待验证] 标注；纠错结论应记录到 MIGRATION.md，不跟随在研
 * 资料直接改动运行代码。
 *
 * ── N100 与标准 RISC-V 的 CSR 差异（完整清单见 BUILD.md 7.9）───────
 *  A. mtvec(0x305) 只读：写被忽略，宏控跳过写。
 *  B. 0x320 地址冲突：标准是 mucounteren，N100 复用为 mcountinhibit。
 *  C. CLIC 向量中断扩展：mtvt/mnxti/mnvec/jalmnxti/push*（标准无）。
 *  D. E203 定时器 CSR 化：mtime/mtimecmp 标准为内存映射，这里做成 CSR。
 *  E. WFE/TXEVT 睡眠扩展：标准 RISC-V 仅有 WFI。
 *  F. IRQC 中断源控制：E203 CLIC 特有（标准无）。
 */

#ifndef ZEPHYR_SOC_RISCV_SPKSILICON_COMMON_N100_CSR_H_
#define ZEPHYR_SOC_RISCV_SPKSILICON_COMMON_N100_CSR_H_

#include <zephyr/arch/riscv/csr.h>

/*
 * ── CLIC 向量中断扩展（标准 RISC-V 无）─────────────────────────────
 * 注意：除 mtvt(0x307) 外，下列扩展 CSR 均未收录于手册 4.10 CSR 总表
 * （表 4-3），仅来自 startup.H。结合 N100"不支持嵌套/尾链"，疑已精简，
 * 待实测确认（见 BUILD.md 7.9 待验证项）。
 */
#define N100_CSR_MTVT           0x307  /* 中断向量表基址；N100 只读、固定 0x30800（手册 4.10.3.10） */
#define N100_CSR_MTVT2          0x7EC  /* 第二向量表基址 [手册未收录，待验证] */
#define N100_CSR_MNXTI          0x345  /* CLIC：下一待处理中断号 [手册未收录，待验证] */
#define N100_CSR_MNVEC          0x7C3  /* CLIC：向量化入口地址 [手册未收录，待验证] */
#define N100_CSR_JALMNXTI       0x7ED  /* 跳转取下一中断（尾链优化）[手册未收录，待验证] */
#define N100_CSR_PUSHMCAUSE     0x7EE  /* 压栈 mcause（嵌套用）[手册未收录，待验证] */
#define N100_CSR_PUSHMEPC       0x7EF  /* 压栈 mepc（嵌套用）[手册未收录，待验证] */
#define N100_CSR_PUSHMSUBM      0x7EB  /* 压栈 msubm（嵌套用）[手册未收录，待验证] */
#define N100_CSR_MSUBM          0x7C4  /* 模式子集屏蔽 [手册未收录，待验证] */

/*
 * ── E203 定时器 / 软件中断 CSR 化 ─────────────────────────────────
 * 标准 RISC-V 中 mtime/mtimecmp 是内存映射（CLINT/ACLINT），E203 为面积
 * 优化做成 CSR。注意驱动代码不能按标准内存映射方式访问。
 */
#define N100_CSR_MSIP           0xBD8  /* 机器软件中断等待位 */
#define N100_CSR_MTIMECMP       0xBD9  /* 计时器比较值 */
#define N100_CSR_MTIME          0xBDA  /* 计时器计数（默认开启） */
#define N100_CSR_MSTOP          0xBDB  /* 计时器开关（1 停止 / 0 计数），低功耗设计：停 mtime 省电 */

/*
 * ── E203 CLIC 中断源控制（标准无；手册 4.10 总表名为 irqc*）────────
 */
#define N100_CSR_IRQCIP         0xBD0  /* 中断源等待标志（bit0 soft/1 timer/2 memerr/3-31 ext0-28） */
#define N100_CSR_IRQCIE         0xBD1  /* 中断源使能控制 */
#define N100_CSR_IRQCLVL        0xBD2  /* 中断源电平触发控制 */
#define N100_CSR_CEDGE          0xBD3  /* 中断源边沿触发控制（手册名 irqcedge） */
#define N100_CSR_IRQCINFO       0xBD4  /* 中断信息：中断源个数（bit5:0） */

/*
 * ── WFE / 睡眠扩展（标准 RISC-V 仅有 WFI）────────────────────────
 */
#define N100_CSR_WFE            0x810  /* Wait-for-Event（0 中断唤醒 / 1 event 唤醒） */
#define N100_CSR_SLEEPVALUE     0x811  /* WFI 休眠模式（1 深度 / 0 浅度） */
#define N100_CSR_TXEVT          0x812  /* 发送 Event（写 1 产生单脉冲） */

#define N100_CSR_MMISC_CTL      0x7D0  /* 杂项控制 [手册未收录，待验证] */

/*
 * ── 地址冲突：0x320 ──────────────────────────────────────────────
 * 标准 RISC-V 的 0x320 是 mucounteren（U 模式计数器使能）；N100 把它复用为
 * mcountinhibit（计数器抑制，低功耗设计：计数器计数消耗动态功耗，可软件关停）。
 *   位域（A20X 用户手册 v0.5.9 4.10.4.1 表 4-9）：bit0 CY=1 关 mcycle；
 *   bit2 IR=1 关 minstret；bit1 / bit31:3 为 Reserved（恒 0）。同址不同义，
 *   勿与标准 mucounteren 混用。
 */
#define N100_CSR_MCOUNTINHIBIT       0x320
#define N100_CSR_MCOUNTINHIBIT_CY    0x1   /* bit0：1 关 mcycle 计数 */
#define N100_CSR_MCOUNTINHIBIT_IR    0x4   /* bit2：1 关 minstret 计数 */

/*
 * 访问封装：仅在 N100 特化路径提供，避免在标准 RISC-V 路径误用特化 CSR。
 * 底层复用 zephyr 的 csr_read/csr_write（展开为 csrr/csrw 指令）。
 */
#if defined(CONFIG_NUCLEI_N100_SPECIAL)

#define n100_csr_read(csr)         csr_read(csr)
#define n100_csr_write(csr, v)     csr_write(csr, v)
#define n100_csr_set(csr, bits)    csr_set(csr, bits)
#define n100_csr_clear(csr, bits)  csr_clear(csr, bits)

#endif /* CONFIG_NUCLEI_N100_SPECIAL */

#endif /* ZEPHYR_SOC_RISCV_SPKSILICON_COMMON_N100_CSR_H_ */
