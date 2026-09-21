/*
 * Copyright (c) 2026 Sparkle Silicon Technology Corp., Ltd.
 * SPDX-License-Identifier: Apache-2.0
 *
 * @file clock.h
 * @brief AE201 时钟域管理 —— 主频单一事实源 + 频率拉取 + 变更通知。
 *
 * AE201 时钟模型（对齐固件 AE_CONFIG.H:191-198）：
 *   - 两个主频源：80M 高速（硬件固定）+ 32.768K 低速（OSC trim 目标）
 *   - 主频 = 80M / (SYSCTL_CLKDIV_OSC80M + 1)，CLKDIV_OSC80M 为可写寄存器；
 *     默认值由 BootROM 按 FlashInfo.MainFrequency 配好（CHIP_CLOCK_SWITCH=4 → 20M，
 *     硬件限制时 clamp，如 AE201N 最高 40M），Zephyr 侧只读、不主动重配
 *   - 各外设再经自身 CLKDIV 二次分频（如 UART = 主频 / (CLKDIV_UART + 1)）
 *   - Timer 也走 80M 分频（可改）；32.768K 低速源供 RTC 等慢速计时
 *
 * 本模块提供三层：
 *   1. 单一事实源 AE201_CLOCK_SRC_HZ（替代散落的 80M/20M 硬编码）
 *   2. 拉取  ae201_clock_freq_get()：读分频寄存器动态算某域当前频率
 *   3. 推送  ae201_clock_change_register()/unregister() + div_set() 触发：
 *      分频改写后通知订阅该域的模块重算（逐时钟域事件）
 *
 * ⚠️ 与 sysctl 的边界：sysctl.c 是「纯 MMIO、无全局变量、无 LOG」的早期
 *   路径安全模块（wdt.c 的 _WdogInit 在 BSS 清零前经 clock_div_get 间接调用）。
 *   而通知表是全局状态（BSS 段），故通知机制落在本独立模块。freq_get()
 *   只读寄存器 + 宏计算（零全局符号），保持早期路径安全；通知表仅在
 *   register/div_set 的正常路径被访问。
 */

#ifndef __RISCV_SPKSILICON_AE201_CLOCK_H_
#define __RISCV_SPKSILICON_AE201_CLOCK_H_

#include <stdint.h>

/* 高速源：80M 内部高速振荡器（硬件固定，外设时钟均由此分频）。 */
#define AE201_CLOCK_SRC_HZ 80000000UL
/* 低速源：32.768K 内部低速振荡器（OSC trim 目标，精确值；RTC 等慢速计时用。
 * 固件 AE_CONFIG.H 用 32000 整数近似 CHIP_CLOCK_INT_LOW）。 */
#define AE201_CLOCK_LOW_SRC_HZ 32768UL

/* 主频分频 div（= CLKDIV_OSC80M 寄存器值，= FlashInfo.MainFrequency - 1）的合法
 * 边界，关联 BootROM AE_INIT.c 主频配置（型号主频上限见 ec103nto_efuse_260127.xlsx）：
 *   - 上限 63：最低主频 80M/(63+1) = 1.25M（IC 文档 + 流片实测的最低值，6 bit）。
 *     div > 31（主频 < 2.5M）属低频，写分频时 LOG_WRN 提示（见 AE201_CLOCK_DIV_WARN）
 *   - 下限按型号放开（Kconfig choice SPK32AE201_SKU）：AE201N/B=40M（div>=1），
 *     AE201E=80M（div>=0），见下方 AE201_CLOCK_DIV_MIN 定义。
 */
#define AE201_CLOCK_DIV_MAX 63
/* div 超过此值（主频 < 2.5M）属低频，写主频分频时提示 warning。 */
#define AE201_CLOCK_DIV_WARN 31
/* 最高主频上限，按型号由 Kconfig choice SPK32AE201_SKU 决定（值取
 * CONFIG_AE201_CLOCK_MAX_HZ）：AE201N/B=40M，AE201E=80M。
 * 强制 (int32_t) 避免与 clamp 的 int32_t div 比较时发生无符号提升（否则 div=-1
 * 下溢时 < 判断失效，漏掉下限保护）。 */
#define AE201_CLOCK_MAX_HZ CONFIG_AE201_CLOCK_MAX_HZ
#define AE201_CLOCK_DIV_MIN ((int32_t)(AE201_CLOCK_SRC_HZ / AE201_CLOCK_MAX_HZ - 1U))

/**
 * @brief AE201 时钟域。
 * @note 首期只覆盖当前实际消费者（主频 + UART）。TMR0~3 的时钟源
 *       （高频/低频）待芯片资料确认后按需扩展。
 */
enum ae201_clock_domain{
	AE201_CLOCK_DOMAIN_OSC80M, /* 主频：CPU / WDT / 多数外设上游 */
	AE201_CLOCK_DOMAIN_UART,   /* UART 波特率时钟 */

	AE201_CLOCK_DOMAIN_COUNT
};

/** @brief 频率变更回调。@param domain 变更的时钟域 @param freq_hz 变更后频率。 */
typedef void (*ae201_clock_change_cb)(enum ae201_clock_domain domain,
					  uint32_t freq_hz);

/**
 * @brief 早期初始化主频（reset.S 的 _WdogInit 经此在 BSS 清零前配置 CLKDIV）。
 *
 * 主频目标 Hz 单一事实源 = dts cpu0 clock-frequency（ae201.dts），
 * 寄存器值 div = AE201_CLOCK_SRC_HZ / clock-frequency - 1
 * （= FlashInfo.MainFrequency - 1）。早期路径（BSS 清零前）不能走
 * ae201_clock_div_set（其通知表在 BSS），故直接纯 MMIO 写 CLKDIV_OSC80M、
 * 不触发通知；后续消费者（WDT/UART）在自身初始化时经 freq_get 读到新主频。
 */
void ae201_clock_init(void);

/**
 * @brief 拉取某时钟域当前频率（Hz）。
 *
 * 纯 MMIO 读分频寄存器 + 宏计算，零全局符号，早期路径（BSS 清零前）安全。
 *
 * @param domain 时钟域。
 * @return 该域当前频率；domain 越界返回 0。
 */
uint32_t ae201_clock_freq_get(enum ae201_clock_domain domain);

/**
 * @brief div 值 clamp 保障（纯函数，无全局符号，早期路径安全）。
 *
 * 把主频分频 div 压到合法范围 [AE201_CLOCK_DIV_MIN, AE201_CLOCK_DIV_MAX]：
 * 高于上限强制 63（最低 1.25M），低于下限强制 MIN（0 或 1）。供写主频 /
 * 频率反推 div 的路径做防御，关联 BootROM 的 AE201N 40M 限制。
 *
 * @param div 分频值（有符号，允许频率反推时下溢为负）。
 * @return clamp 后的 div。
 */
uint8_t ae201_clock_div_clamp(int32_t div);

/**
 * @brief 写某时钟域分频值，并通知该域订阅者（逐时钟域事件）。
 *
 * 这是分频的唯一写入口 —— 低功耗 / SPIF 降频流程都走这里，保证通知不漏。
 * 内部调 ae201_sysctl_clock_div_set() 写寄存器，随后遍历该域订阅者回调，
 * 传入变更后的频率。
 *
 * @param domain 时钟域。
 * @param div    分频值（0 = 不分频）。
 * @return 0 成功；domain 越界返回 -EINVAL。
 */
int ae201_clock_div_set(enum ae201_clock_domain domain, uint8_t div);

/**
 * @brief 订阅某时钟域的频率变更通知。
 *
 * 主频域（AE201_CLOCK_DOMAIN_OSC80M）是「广播」语义：主频为唯一上游源，
 * 改写后广播给所有订阅了主频的外设（SPIF/TIMER/UART/SMBUS/ADC/WDT 等），
 * 回调收到新主频 Hz，自行重算时序。槽位上限 12（AE201_MAIN_CLOCK_LISTENER_MAX）。
 * 其余域（如 UART）是「逐域」语义：只通知该域订阅者，槽位上限 4。
 *
 * @param domain 时钟域。
 * @param cb     回调（同一回调重复注册会被去重忽略）。
 */
void ae201_clock_change_register(enum ae201_clock_domain domain,
				 ae201_clock_change_cb cb);

/**
 * @brief 退订某时钟域的频率变更通知。
 * @param domain 时钟域。
 * @param cb     回调。
 */
void ae201_clock_change_unregister(enum ae201_clock_domain domain,
				   ae201_clock_change_cb cb);

#endif /* __RISCV_SPKSILICON_AE201_CLOCK_H_ */
