/*
 * @Author: daweslinyu daowes.ly@qq.com
 * @Date: 2026-09-03 17:47:40
 * @LastEditors: daweslinyu daowes.ly@qq.com
 * @LastEditTime: 2026-09-07 16:37:53
 * @FilePath: /SPK32AE103NTO/west/zephyr_fork/soc/riscv/spksilicon/ae103/soc.c
//  * @Description: 这是默认设置,请设置`customMade`, 打开koroFileHeader查看配置 进行设置: https://github.com/OBKoro1/koro1FileHeader/wiki/%E9%85%8D%E7%BD%AE
 */
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

