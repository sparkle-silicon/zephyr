/*
 * Copyright (c) 2026 Sparkle Silicon Technology Corp., Ltd.
 * SPDX-License-Identifier: Apache-2.0
 *
 * @file clock.c
 * @brief AE201 时钟域管理实现（主频事实源 + 频率拉取 + 变更通知）。
 *
 * 设计要点（见 clock.h 文件头）：
 *   - freq_get 用 switch 映射 CLKDIV offset（零全局符号），早期路径安全；
 *   - 通知表是 static 二维数组（BSS 段），仅 register/div_set 正常路径访问；
 *   - div_set 是分频唯一写入口，写寄存器后遍历该域订阅者回调。
 */

#include <errno.h>
#include <stdint.h>

#include <zephyr/devicetree.h>
#include <zephyr/init.h>
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(ae201_clock, LOG_LEVEL_WRN);

#include "clock.h"
#include "sysctl.h"

/* ================= 主频初始化 ======================================= */
void ae201_clock_init(void)
{
	/* 主频目标 Hz 单一事实源 = dts cpu0 clock-frequency（ae201_nto.dts）。
	 * CLKDIV_OSC80M 是寄存器值，主频 = 80M / (div + 1)，故
	 * div = 80M / hz - 1（= FlashInfo.MainFrequency - 1）。早期（BSS 前）
	 * 不能走 ae201_clock_div_set（通知表在 BSS），直接纯 MMIO 写。
	 * div 经 clamp 压到 6bit 合法范围 [MIN,MAX]：dts 配错（hz>80M 下溢 /
	 * hz 过小上溢）时不至于把 6bit 之外的位写进 CLKDIV 寄存器。 */
	uint32_t hz = DT_PROP(DT_NODELABEL(cpu0), clock_frequency);
	uint8_t div = ae201_clock_div_clamp((int32_t)(AE201_CLOCK_SRC_HZ / hz - 1U));

	ae201_sysctl_clock_div_set(AE201_SYSCTL_CLKDIV_OSC80M_OFFSET, div);
}

#ifndef CONFIG_WDOG_INIT
/* WDT 未开 → 主频没有 reset.S 的 _WdogInit 可挂（wdt.c 受 CONFIG_WDOG_INIT
 * 编译控制，整文件不编），改在早期 SYS_INIT 补配。须早于 UART driver
 * （PRE_KERNEL_1 / CONFIG_SERIAL_INIT_PRIORITY=50），否则 UART 按错误的 80M
 * 算波特率。level=PRE_KERNEL_1 + CONFIG_KERNEL_INIT_PRIORITY_DEFAULT(40)
 * 保证先跑（40 < 50）。 */
static int ae201_clock_prep(void)
{
	ae201_clock_init();
	return 0;
}
SYS_INIT(ae201_clock_prep, PRE_KERNEL_1, CONFIG_KERNEL_INIT_PRIORITY_DEFAULT);
#endif
/* ================= 通知表 =========================================== */
/* 主频（OSC80M）变化广播列表 —— 主频是唯一上游源，改写后广播给所有依赖
 * 其时序的外设。独立于下方 per-domain 二维表：主频消费者多（约 12 类），
 * 而单外设域通常只 1 个订阅者，统一二维表会两头不讨好（主频域槽不够、
 * 外设域槽浪费）。
 *
 * 外设订阅主频变化：ae201_clock_change_register(AE201_CLOCK_DOMAIN_OSC80M, cb)，
 * 回调收到新主频 Hz，自行重算时序（波特率/超时/延迟/分频等）。
 *
 * 主频消费者清单（回调重算项；数字=实例数，订阅者是驱动模块而非实例，
 * 故槽位上限 12 足够）。eSPI/I3C 等 Host 交互模块走 Host 域时钟（主机
 * 提供 eCLK），不依赖本地主频，故不在清单内：
SPIF		1 OSC80M->External Flash Disable
TIMER0-3	4 CLKDIV_TIMER->New Time Count
KBS/PS2		3 Delay Time
SMBUS0-5	6 speed
RTC		1 OSC32K->Wait Init
UART0-2A-B	4 Baud
SPIM/SPIS	2 speed
PWM0-7/TACH0-3  12 div
PECI		1 CLKDIV_PECI
ADC		8 CLKDIV_OSC80M_ADC
WDT		1 Feed Time
SWUC		1 ?（无 CLKDIV，待确认）
OWI(LEDC)	1 CLKDIV_OWI
CEC		1 CLKDIV_CEC（HDMI 消费电子控制）
GPIODB		1 CLKDIV_GPIODB（GPIO 去抖）
 */
#define AE201_MAIN_CLOCK_LISTENER_MAX 12
static ae201_clock_change_cb main_clock_listeners[AE201_MAIN_CLOCK_LISTENER_MAX];

/* 外设域（UART 等有独立 CLKDIV 的域）订阅表：每域少量订阅者。
 * 主频域（OSC80M）不走此表，走上面的广播列表。 */
#define AE201_CLOCK_LISTENER_MAX 4
static ae201_clock_change_cb listeners[AE201_CLOCK_DOMAIN_COUNT][AE201_CLOCK_LISTENER_MAX];

/* ================= 域 → CLKDIV offset 映射 =========================== */
/*
 * 用 switch 而非查表数组：freq_get 早期路径（BSS 清零前）安全，零全局符号。
 * offset 值对齐 sysctl.h 的 AE201_SYSCTL_CLKDIV_*_OFFSET。
 */
static uint32_t domain_clkdiv_offset(enum ae201_clock_domain domain)
{
	switch (domain)
	{
		case AE201_CLOCK_DOMAIN_OSC80M:
			return AE201_SYSCTL_CLKDIV_OSC80M_OFFSET;
		case AE201_CLOCK_DOMAIN_UART:
			return AE201_SYSCTL_CLKDIV_UART_OFFSET;
		default:
			return 0U;
	}
}

/* ================= 频率拉取 ========================================= */
uint32_t ae201_clock_freq_get(enum ae201_clock_domain domain)
{
	uint32_t main_freq = AE201_CLOCK_SRC_HZ /
		(ae201_sysctl_clock_div_get(AE201_SYSCTL_CLKDIV_OSC80M_OFFSET) + 1U);

	switch (domain)
	{
		case AE201_CLOCK_DOMAIN_OSC80M:
			return main_freq;
		case AE201_CLOCK_DOMAIN_UART:
			/* UART = 主频 / (CLKDIV_UART + 1)，复位值 0 = 不分频。 */
			return main_freq /
				(ae201_sysctl_clock_div_get(AE201_SYSCTL_CLKDIV_UART_OFFSET) + 1U);
		default:
			return 0U;
	}
}

/* ================= div clamp 保障 ==================================== */
uint8_t ae201_clock_div_clamp(int32_t div)
{
	/* 上限：div 有效 6 bit（0~63），>63 强制压到 63（最低主频 1.25M）。 */
	if (div > AE201_CLOCK_DIV_MAX)
	{
		div = AE201_CLOCK_DIV_MAX;
	}
	/* 下限：低于 MIN 强制抬到 MIN（= 最高主频上限，按型号 40M/80M，防超频出问题）。 */
	if (div < AE201_CLOCK_DIV_MIN)
	{
		div = (int32_t)AE201_CLOCK_DIV_MIN;
	}
	return (uint8_t)div;
}

/* ================= 变更通知 ========================================= */
/* 选出 domain 对应的订阅列表与槽位上限：主频域走广播列表（消费者多），
 * 其余域走 per-domain 表（每域少量订阅者）。register/unregister/notify
 * 三处共享这一份分流，后续新增域只改这里。 */
static void listener_list(enum ae201_clock_domain domain,
			  ae201_clock_change_cb **list, uint32_t *max)
{
	if (domain == AE201_CLOCK_DOMAIN_OSC80M)
	{
		*list = main_clock_listeners;
		*max = AE201_MAIN_CLOCK_LISTENER_MAX;
	}
	else
	{
		*list = listeners[domain];
		*max = AE201_CLOCK_LISTENER_MAX;
	}
}

static void clock_notify(enum ae201_clock_domain domain)
{
	ae201_clock_change_cb *list;
	uint32_t max;
	uint32_t freq_hz = ae201_clock_freq_get(domain);

	listener_list(domain, &list, &max);
	for (uint32_t i = 0U; i < max; i++)
	{
		if (list[i] != NULL)
		{
			list[i](domain, freq_hz);
		}
	}
}

int ae201_clock_div_set(enum ae201_clock_domain domain, uint8_t div)
{
	if (domain >= AE201_CLOCK_DOMAIN_COUNT)
	{
		return -EINVAL;
	}

	/* 主频域写分频前做边界保障（按型号上限 40M/80M）；其他域暂不 clamp。 */
	if (domain == AE201_CLOCK_DOMAIN_OSC80M)
	{
		div = ae201_clock_div_clamp((int32_t)div);
		/* div > 31（主频 < 2.5M）属低频，提示；1.25M（div=63）才是硬下限。 */
		if (div > AE201_CLOCK_DIV_WARN)
		{
			LOG_WRN("main clock div=%d (< 2.5MHz, min 1.25MHz)", div);
		}
	}

	ae201_sysctl_clock_div_set(domain_clkdiv_offset(domain), div);
	clock_notify(domain);

	return 0;
}

void ae201_clock_change_register(enum ae201_clock_domain domain,
				 ae201_clock_change_cb cb)
{
	if (domain >= AE201_CLOCK_DOMAIN_COUNT || cb == NULL)
	{
		return;
	}

	ae201_clock_change_cb *list;
	uint32_t max;

	listener_list(domain, &list, &max);
	for (uint32_t i = 0U; i < max; i++)
	{
		if (list[i] == cb)
		{
			return; /* 去重：同一回调重复注册忽略 */
		}
		if (list[i] == NULL)
		{
			list[i] = cb;
			return;
		}
	}
}

void ae201_clock_change_unregister(enum ae201_clock_domain domain,
				   ae201_clock_change_cb cb)
{
	if (domain >= AE201_CLOCK_DOMAIN_COUNT || cb == NULL)
	{
		return;
	}

	ae201_clock_change_cb *list;
	uint32_t max;

	listener_list(domain, &list, &max);
	for (uint32_t i = 0U; i < max; i++)
	{
		if (list[i] == cb)
		{
			list[i] = NULL;
			return;
		}
	}
}
