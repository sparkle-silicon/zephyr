/*
 * Copyright (c) 2026 Sparkle Silicon Technology Corp., Ltd.
 * SPDX-License-Identifier: Apache-2.0
 *
 * @file wdt.c
 * @brief AE103 看门狗早期初始化（_WdogInit）+ 后续 WDT 驱动扩展点。
 *
 * 背景：arch/riscv/core/reset.S 在 `call z_prep_c` 之前有：
 *     #ifdef CONFIG_WDOG_INIT
 *         call _WdogInit
 *     #endif
 * 用于在 C 领域就绪前喂狗/禁狗，防止启动最早阶段（BSS 清零 + .data
 * 拷贝之前）看门狗超时复位。本文件由 reset.S 的调用点经
 * CONFIG_WDOG_INIT 接入（见 CMakeLists.txt 的 zephyr_sources_ifdef）。
 *
 * ⚠️ 早期阶段约束（_WdogInit 必须遵守，否则会读到未初始化的内存）：
 *   1. 不得引用任何全局变量 —— .data 尚未从 flash 拷贝、.bss 尚未清零；
 *   2. 不得使用 LOG_* —— deferred logging 依赖 .bss 里的后端状态；
 *   3. 仅允许：栈局部变量 + sys_read8/sys_write8 直接 MMIO 访问。
 *   （sp 已由 reset.S 设置，栈可用；gp 已由 vector.S 的 __start 设置。）
 */

#include <zephyr/sys/sys_io.h>

/* --- 看门狗寄存器布局（待芯片资料确认） --------------------------- */
/* TODO(林雨)：从 SPK32A20X 用户手册查 AE103 WDT 寄存器基址与位域。
 * 已知线索：WDT 中断号 = 19（向量表 .word WDT_HANDLER//19）；
 * SYSCTL 复位域 @ 0x30400（soc.c sys_arch_reboot 的 TODO 同源）。
 */

/**
 * @brief 早期看门狗初始化，由 reset.S 在 C 领域就绪前调用。
 */
void _WdogInit(void)
{
	/*
	 * TODO(林雨)：在此实现早期看门狗初始化（5-10 行）——
	 *   1. 读 WDT 控制寄存器，判断看门狗是否默认使能；
	 *   2. 若使能：喂一次狗（写喂狗/重装载寄存器），或直接禁用；
	 *   3. 按 AE103 寄存器 8-bit 映射访问（参考 timer.c 的 sys_write8）。
	 *
	 * 示例骨架（寄存器名/地址待替换）：
	 *   const uintptr_t wdt_base = 0x????U;
	 *   sys_write8(0x??, wdt_base + WDT_CTRL_OFF);   // 禁狗/喂狗
	 */
}
