/*
 * Copyright (c) 2026 Sparkle Silicon Technology Corp., Ltd.
 * SPDX-License-Identifier: Apache-2.0
 *
 * @file wdt.c
 * @brief AE103 看门狗（WDT）驱动 —— Synopsys DesignWare DW_apb_wdt 兼容。
 *
 * 背景：arch/riscv/core/reset.S 在 `call z_prep_c` 之前有：
 *     #ifdef CONFIG_WDOG_INIT
 *         call _WdogInit
 *     #endif
 * 用于在 C 领域就绪前喂狗，防止启动最早阶段（BSS 清零 + .data
 * 拷贝之前）看门狗超时复位。本文件由 reset.S 的调用点经
 * CONFIG_WDOG_INIT 接入（见 CMakeLists.txt 的 zephyr_sources_ifdef）。
 *
 * ⚠️ 早期阶段约束（_WdogInit 必须遵守，否则会读到未初始化的内存）：
 *   1. 不得引用任何全局变量 —— .data 尚未从 flash 拷贝、.bss 尚未清零；
 *   2. 不得使用 LOG_* —— deferred logging 依赖 .bss 里的后端状态；
 *      → 正常早期路径不触发 LOG（WDT 复位后 STAT 清 0、200ms 超时在
 *        档位内），若 WDT 异常触发 LOG_ERR/LOG_WRN 属已知限制；
 *   3. 仅允许：栈局部变量 + sys_read8/sys_write8 直接 MMIO 访问。
 *   （sp 已由 reset.S 设置，栈可用；gp 已由 vector.S 的 __start 设置。）
 *
 * 命名规则：寄存器宏 / 位域宏统一在 wdt.h（AE103_WDT_*，对齐 AE_REG.H），
 * 对外函数蛇形化为 ae103_wdt_*。主频经 clock.h 的 ae103_clock_freq_get
 * 获取（纯 MMIO 读分频寄存器，无 LOG / 无全局，满足早期路径约束）。
 */

#include <zephyr/arch/riscv/sys_io.h>   /* sys_read8/sys_write8/sys_read32 架构实现 */
#include <zephyr/sys/util.h>            /* BIT/GENMASK/FIELD_PREP/FIELD_GET */
#include <zephyr/types.h>               /* uint8_t/uint32_t */
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(ae103_wdt, LOG_LEVEL_WRN);

#include "clock.h"    /* ae103_clock_freq_get（主频拉取，早期路径纯 MMIO） */
#include "wdt.h"

/* ================= 寄存器访问（宏嵌套） ==============================
 * 参考固件 AE_REG.H 的 REG_ADDR → REG8 → WDT_REG 三层嵌套，
 * 适配 zephyr 的 sys_read8/sys_write8 函数访问（非指针解引用）。
 */
#if 1/* 采用 mask 的格式 */
#define WDT_REG_ADDR(offset)        	((AE103_WDT_BASE_ADDR) + ((offset)&AE103_WDT_OFFSET_MASK))
#else
#define WDT_REG_ADDR(offset)        	((AE103_WDT_BASE_ADDR) + (offset))
#endif

static ALWAYS_INLINE uint8_t wdt_read8(mem_addr_t offset)
{
	return sys_read8(WDT_REG_ADDR(offset));
}
static ALWAYS_INLINE void wdt_write8(mem_addr_t offset, uint8_t val)
{
	sys_write8((uint8_t)(val), WDT_REG_ADDR(offset));
}
#define wdt_read(offset)        wdt_read8(offset)
#define wdt_write(offset, val)  wdt_write8(offset, val)

// #define CONFIG_STANDARD_WATCHDOG
// #undef CONFIG_STANDARD_WATCHDOG
#ifdef CONFIG_STANDARD_WATCHDOG
/* ================= zephyr wdt_dw.h 参考（不编译） ====================
 * 以下为参考 drivers/watchdog/wdt_dw.h（zephyr 上游 DW_apb_wdt 驱动）
 * 的 dw_wdt_* 寄存器级 API，保留作对照。寄存器/位域已统一到 wdt.h。
 * ------------------------------------------------------------------ */

/** @brief Enable watchdog */
static inline void dw_wdt_enable(void)
{
	uint8_t control = wdt_read(AE103_WDT_CR_OFFSET);

	control |= AE103_WDT_CR_EN;
	wdt_write(AE103_WDT_CR_OFFSET, control);
}

/** @brief Set response mode（true=中断，false=复位） */
static inline void dw_wdt_response_mode_set(const bool mode)
{
	uint8_t control = wdt_read(AE103_WDT_CR_OFFSET);

	if (mode)
	{
		control |= AE103_WDT_CR_RMOD_INTR;
	}
	else
	{
		control &= ~AE103_WDT_CR_RMOD_INTR;/* AE103_WDT_CR_RMOD_RESET */
	}
	wdt_write(AE103_WDT_CR_OFFSET, control);
}

/** @brief Set reset pulse length（2~256 pclk cycles） */
static inline void dw_wdt_reset_pulse_length_set(const uint8_t pclk_cycles)
{
	uint8_t control = wdt_read(AE103_WDT_CR_OFFSET);

	control &= ~AE103_WDT_CR_RPL_MASK;
	control |= FIELD_PREP(AE103_WDT_CR_RPL_MASK, pclk_cycles);
	wdt_write(AE103_WDT_CR_OFFSET, control);
}

/** @brief Set timeout period */
static inline void dw_wdt_timeout_period_set(const uint32_t timeout_period)
{
	uint8_t timeout = wdt_read(AE103_WDT_TORR0_OFFSET);

	timeout &= ~AE103_WDT_TORR_TOP_MASK;
	timeout |= FIELD_PREP(AE103_WDT_TORR_TOP_MASK, timeout_period);
	wdt_write(AE103_WDT_TORR0_OFFSET, timeout);
}

/** @brief Get actual timeout period range */
static inline uint8_t dw_wdt_timeout_period_get(void)
{
	return FIELD_GET(AE103_WDT_TORR_TOP_MASK, wdt_read(AE103_WDT_TORR0_OFFSET));
}

/** @brief Timeout period for initialization（TORR1 高字节） */
static inline void dw_wdt_timeout_period_init_set(const uint8_t timeout_period)
{
	uint8_t timeout = wdt_read(AE103_WDT_TORR1_OFFSET);

	timeout &= ~AE103_WDT_TORR_TOP_INIT_MASK;
	timeout |= FIELD_PREP(AE103_WDT_TORR_TOP_INIT_MASK, timeout_period);
	wdt_write(AE103_WDT_TORR1_OFFSET, timeout);
}

/** @brief Get WDT Current Counter Value Register（CCVR0~3 拼 32-bit） */
static inline uint32_t dw_wdt_current_counter_value_register_get(uint8_t wdt_counter_width)
{
	uint32_t current_counter_value = 0;

	for (uint32_t index = 0; index < 4; index++)
	{
		uint8_t temp = wdt_read(AE103_WDT_CCVR0_OFFSET + index);

		current_counter_value |= ((uint32_t)temp << (index << 3));
	}
	current_counter_value &= (1 << (wdt_counter_width - 1));
	return current_counter_value;
}

/** @brief Counter Restart（喂狗，同时清中断） */
static inline void dw_wdt_counter_restart(void)
{
	wdt_write(AE103_WDT_CRR_OFFSET, AE103_WDT_CRR_CRR);
}

/** @brief Get Interrupt status */
static inline uint8_t dw_wdt_interrupt_status_register_get(void)
{
	return wdt_read(AE103_WDT_STAT_OFFSET);
}

/** @brief Clears the watchdog interrupt（读 EOI 即清，不喂狗） */
static inline void dw_wdt_clear_interrupt(void)
{
	wdt_read(AE103_WDT_EOI_OFFSET);
}

void _WdogInit(void)
{
		/* 先配主频（BSS 前纯 MMIO），WDT 随后按新主频算超时 count —— 保证主频
	 * 配置与 WDT 时间同步。BootROM 不强制切主频，Zephyr 需在此自行配置。 */
	ae103_clock_init();//CONFIG_WDOG_INIT没生效
	/*配置看门狗*/
	dw_wdt_enable();
	dw_wdt_timeout_period_set(AE103_WDT_TORR_TOP_2G);
	dw_wdt_counter_restart();
}
#else
/* ================= 实际实现：对齐固件 KERNEL_WATCHDOG.c ============= */

/* 清除看门狗超时中断，但不喂狗。 */
void ae103_wdt_clear_irq(void)
{
	if (wdt_read(AE103_WDT_STAT_OFFSET) & AE103_WDT_STAT_ISR)
	{
		/* Clear interruption */
		wdt_read(AE103_WDT_EOI_OFFSET);
	}
}

/* 清除看门狗超时中断并进行喂狗操作。 */
void ae103_wdt_feed(void)
{
	uint32_t wdt_error_cnt = 0;

	do
	{
		wdt_write(AE103_WDT_CRR_OFFSET, AE103_WDT_CRR_CRR);/* 喂狗 */
		if (!(wdt_read(AE103_WDT_STAT_OFFSET) & AE103_WDT_STAT_ISR))
		{
			break;/* 喂狗成功，中断已清 */
		}
		/* 喂狗后中断仍挂起（异常）：手动清中断后重试 */
		wdt_read(AE103_WDT_EOI_OFFSET);
		wdt_error_cnt++;
	}
	while (wdt_error_cnt < AE103_WDT_FEEDDOG_TIMEOUT);

	if (wdt_error_cnt >= AE103_WDT_FEEDDOG_TIMEOUT)
	{
		LOG_ERR("WDT: Feed Fail\n");
	}
}

/*
 * 看门狗初始化。
 * @param mode 0:超时复位  1:超时中断（AE103_WDT_MODE_RESET / _INTR）
 * @param rpl  复位脉冲长度（AE103_WDT_RPL_2..AE103_WDT_RPL_256）
 * @param top  超时档位（AE103_WDT_TORR_TOP_64K..AE103_WDT_TORR_TOP_2G）
 */
void ae103_wdt_init(uint8_t mode, uint8_t rpl, uint8_t top)
{
	wdt_write(AE103_WDT_CR_OFFSET,
		      FIELD_PREP(AE103_WDT_CR_RPL_MASK, rpl) |
		      (mode ? AE103_WDT_CR_RMOD_INTR : AE103_WDT_CR_RMOD_RESET) |
		      AE103_WDT_CR_EN);
	wdt_write(AE103_WDT_TORR0_OFFSET,
		      (top > AE103_WDT_TORR_TOP_2G ? AE103_WDT_TORR_TOP_2G : top));
	ae103_wdt_feed();
}

/*
 * 看门狗初始化（按重装载计数值）。
 * @param mode  0:超时复位  1:超时中断
 * @param rpl   复位信号保持时长倍率
 * @param count pclk_wdt 计数值
 */
void ae103_wdt_init_count(uint8_t mode, uint8_t rpl, uint32_t count)
{
	uint8_t top = 0;

	while (top <= AE103_WDT_TORR_TOP_2G)
	{
		uint32_t recount = (1UL << (16 + top));/* cnt */

		if (recount >= count)
		{
			break;/* 结果大于 count */
		}
		if (top < AE103_WDT_TORR_TOP_2G)
		{
			top++;/* 优先加计数 */
		}
		else
		{
			LOG_WRN("WDT: Count Warring\n");
			break;/* 超过阈值了 */
		}
	}
	ae103_wdt_init(mode, rpl, top);
}

/*
 * 看门狗初始化（按超时毫秒数）。
 * @param mode 0:超时复位  1:超时中断
 * @param rpl  复位信号保持时长倍率
 * @param ms   重装载时间（80M 下最低 0.8192ms）
 */
void ae103_wdt_init_time(uint8_t mode, uint8_t rpl, uint32_t ms)
{
	uint32_t count = ae103_clock_freq_get(AE103_CLOCK_DOMAIN_OSC80M) / 1000U;/* 1ms 的 count */

	if (((1UL << (16 + AE103_WDT_TORR_TOP_2G)) / count) < ms)
	{
		count = (1UL << (16 + AE103_WDT_TORR_TOP_2G));/* 超出可表示范围，用最大超时 */
	}
	else
	{
		count *= ms;
	}
	ae103_wdt_init_count(mode, rpl, count);
}

/* 早期默认配置（_WdogInit 使用） */
#define AE103_WDT_DEFAULT_MODE  AE103_WDT_MODE_RESET
#define AE103_WDT_DEFAULT_RPL   AE103_WDT_RPL_256
#define AE103_WDT_DEFAULT_MS    200

/*
 * reset.S 在 z_prep_c 之前调用的 SoC hook（符号名 _WdogInit 为 arch→SoC
 * 接口约定，见文件头）。只做最小寄存器配置 + 喂狗，不触 LOG、不触全局。
 */
void _WdogInit(void)
{
	/* 先配主频（BSS 前纯 MMIO），WDT 随后按新主频算超时 count —— 保证主频
	 * 配置与 WDT 时间同步。BootROM 不强制切主频，Zephyr 需在此自行配置。 */
	ae103_clock_init();//CONFIG_WDOG_INIT没生效
	ae103_wdt_init_time(AE103_WDT_DEFAULT_MODE, AE103_WDT_DEFAULT_RPL, AE103_WDT_DEFAULT_MS);
}

#endif
