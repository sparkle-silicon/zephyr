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
