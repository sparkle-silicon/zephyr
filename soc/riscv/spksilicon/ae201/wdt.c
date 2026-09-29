/*
 * Copyright (c) 2026 Sparkle Silicon Technology Corp., Ltd.
 * SPDX-License-Identifier: Apache-2.0
 *
 * @file wdt.c
 * @brief AE201 看门狗（WDT）驱动 —— Synopsys DesignWare DW_apb_wdt 兼容。
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
 * 命名规则：寄存器宏 / 位域宏统一在 wdt.h（AE201_WDT_*，对齐 AE_REG.H），
 * 对外函数蛇形化为 ae201_wdt_*。主频经 clock.h 的 ae201_clock_freq_get
 * 获取（纯 MMIO 读分频寄存器，无 LOG / 无全局，满足早期路径约束）。
 */

#include <zephyr/arch/riscv/sys_io.h>   /* sys_read8/sys_write8/sys_read32 架构实现 */
#include <zephyr/sys/util.h>            /* BIT/GENMASK/FIELD_PREP/FIELD_GET */
#include <zephyr/types.h>               /* uint8_t/uint32_t */
#include <zephyr/logging/log.h>
// LOG_MODULE_REGISTER(ae201_wdt, LOG_LEVEL_WRN);/*CONFIG_WDT_LOG_LEVEL*/

#include "clock.h"    /* ae201_clock_freq_get（主频拉取，早期路径纯 MMIO） */
#include "wdt.h"

/* ================= 寄存器访问（宏嵌套） ==============================
 * 参考固件 AE_REG.H 的 REG_ADDR → REG8 → WDT_REG 三层嵌套，
 * 适配 zephyr 的 sys_read8/sys_write8 函数访问（非指针解引用）。
 */
#if 1/* 采用 mask 的格式 */
#define WDT_REG_ADDR(offset)        	((AE201_WDT_BASE_ADDR) + ((offset)&AE201_WDT_OFFSET_MASK))
#else
#define WDT_REG_ADDR(offset)        	((AE201_WDT_BASE_ADDR) + (offset))
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
	uint8_t control = wdt_read(AE201_WDT_CR_OFFSET);

	control |= AE201_WDT_CR_EN;
	wdt_write(AE201_WDT_CR_OFFSET, control);
}

/** @brief Set response mode（true=中断，false=复位） */
static inline void dw_wdt_response_mode_set(const bool mode)
{
	uint8_t control = wdt_read(AE201_WDT_CR_OFFSET);

	if (mode)
	{
		control |= AE201_WDT_CR_RMOD_INTR;
	}
	else
	{
		control &= ~AE201_WDT_CR_RMOD_INTR;/* AE201_WDT_CR_RMOD_RESET */
	}
	wdt_write(AE201_WDT_CR_OFFSET, control);
}

/** @brief Set reset pulse length（2~256 pclk cycles） */
static inline void dw_wdt_reset_pulse_length_set(const uint8_t pclk_cycles)
{
	uint8_t control = wdt_read(AE201_WDT_CR_OFFSET);

	control &= ~AE201_WDT_CR_RPL_MASK;
	control |= FIELD_PREP(AE201_WDT_CR_RPL_MASK, pclk_cycles);
	wdt_write(AE201_WDT_CR_OFFSET, control);
}

/** @brief Set timeout period */
static inline void dw_wdt_timeout_period_set(const uint32_t timeout_period)
{
	uint8_t timeout = wdt_read(AE201_WDT_TORR0_OFFSET);

	timeout &= ~AE201_WDT_TORR_TOP_MASK;
	timeout |= FIELD_PREP(AE201_WDT_TORR_TOP_MASK, timeout_period);
	wdt_write(AE201_WDT_TORR0_OFFSET, timeout);
}

/** @brief Get actual timeout period range */
static inline uint8_t dw_wdt_timeout_period_get(void)
{
	return FIELD_GET(AE201_WDT_TORR_TOP_MASK, wdt_read(AE201_WDT_TORR0_OFFSET));
}

/** @brief Timeout period for initialization（TORR1 高字节） */
static inline void dw_wdt_timeout_period_init_set(const uint8_t timeout_period)
{
	uint8_t timeout = wdt_read(AE201_WDT_TORR1_OFFSET);

	timeout &= ~AE201_WDT_TORR_TOP_INIT_MASK;
	timeout |= FIELD_PREP(AE201_WDT_TORR_TOP_INIT_MASK, timeout_period);
	wdt_write(AE201_WDT_TORR1_OFFSET, timeout);
}

/** @brief Get WDT Current Counter Value Register（CCVR0~3 拼 32-bit） */
static inline uint32_t dw_wdt_current_counter_value_register_get(uint8_t wdt_counter_width)
{
	uint32_t current_counter_value = 0;

	for (uint32_t index = 0; index < 4; index++)
	{
		uint8_t temp = wdt_read(AE201_WDT_CCVR0_OFFSET + index);

		current_counter_value |= ((uint32_t)temp << (index << 3));
	}
	current_counter_value &= (1 << (wdt_counter_width - 1));
	return current_counter_value;
}

/** @brief Counter Restart（喂狗，同时清中断） */
static inline void dw_wdt_counter_restart(void)
{
	wdt_write(AE201_WDT_CRR_OFFSET, AE201_WDT_CRR_CRR);
}

/** @brief Get Interrupt status */
static inline uint8_t dw_wdt_interrupt_status_register_get(void)
{
	return wdt_read(AE201_WDT_STAT_OFFSET);
}

/** @brief Clears the watchdog interrupt（读 EOI 即清，不喂狗） */
static inline void dw_wdt_clear_interrupt(void)
{
	wdt_read(AE201_WDT_EOI_OFFSET);
}
#if CONFIG_WDOG_INIT
void _WdogInit(void)
{
		/* 先配主频（BSS 前纯 MMIO），WDT 随后按新主频算超时 count —— 保证主频
	 * 配置与 WDT 时间同步。BootROM 不强制切主频，Zephyr 需在此自行配置。 */
	ae201_clock_init();//CONFIG_WDOG_INIT没生效
	/*配置看门狗*/
	dw_wdt_enable();
	dw_wdt_timeout_period_set(AE201_WDT_TORR_TOP_2G);
	dw_wdt_counter_restart();
}
#else
#endif
#else
/* ================= 实际实现：对齐固件 KERNEL_WATCHDOG.c ============= */

/* 清除看门狗超时中断，但不喂狗。 */
void ae201_wdt_clear_irq(void)
{
	if (wdt_read(AE201_WDT_STAT_OFFSET) & AE201_WDT_STAT_ISR)
	{
		/* Clear interruption */
		wdt_read(AE201_WDT_EOI_OFFSET);
	}
}

/* 清除看门狗超时中断并进行喂狗操作。 */
void ae201_wdt_feed(void)
{
	uint32_t wdt_error_cnt = 0;

	do
	{
		wdt_write(AE201_WDT_CRR_OFFSET, AE201_WDT_CRR_CRR);/* 喂狗 */
		if (!(wdt_read(AE201_WDT_STAT_OFFSET) & AE201_WDT_STAT_ISR))
		{
			break;/* 喂狗成功，中断已清 */
		}
		/* 喂狗后中断仍挂起（异常）：手动清中断后重试 */
		wdt_read(AE201_WDT_EOI_OFFSET);
		wdt_error_cnt++;
	}
	while (wdt_error_cnt < AE201_WDT_FEEDDOG_TIMEOUT);

	if (wdt_error_cnt >= AE201_WDT_FEEDDOG_TIMEOUT)
	{
		// LOG_ERR("WDT: Feed Fail\n");
	}
}

/*
 * 看门狗初始化。
 * @param mode 0:超时复位  1:超时中断（AE201_WDT_MODE_RESET / _INTR）
 * @param rpl  复位脉冲长度（AE201_WDT_RPL_2..AE201_WDT_RPL_256）
 * @param top  超时档位（AE201_WDT_TORR_TOP_64K..AE201_WDT_TORR_TOP_2G）
 */
void ae201_wdt_init(uint8_t mode, uint8_t rpl, uint8_t top)
{
	wdt_write(AE201_WDT_CR_OFFSET,
		      FIELD_PREP(AE201_WDT_CR_RPL_MASK, rpl) |
		      (mode ? AE201_WDT_CR_RMOD_INTR : AE201_WDT_CR_RMOD_RESET) |
		      AE201_WDT_CR_EN);
	wdt_write(AE201_WDT_TORR0_OFFSET,
		      (top > AE201_WDT_TORR_TOP_2G ? AE201_WDT_TORR_TOP_2G : top));
	ae201_wdt_feed();
}

/*
 * 看门狗初始化（按重装载计数值）。
 * @param mode  0:超时复位  1:超时中断
 * @param rpl   复位信号保持时长倍率
 * @param count pclk_wdt 计数值
 */
void ae201_wdt_init_count(uint8_t mode, uint8_t rpl, uint32_t count)
{
	uint8_t top = 0;

	while (top <= AE201_WDT_TORR_TOP_2G)
	{
		uint32_t recount = (1UL << (16 + top));/* cnt */

		if (recount >= count)
		{
			break;/* 结果大于 count */
		}
		if (top < AE201_WDT_TORR_TOP_2G)
		{
			top++;/* 优先加计数 */
		}
		else
		{
			// LOG_WRN("WDT: Count Warring\n");
			break;/* 超过阈值了 */
		}
	}
	ae201_wdt_init(mode, rpl, top);
}

/*
 * 看门狗初始化（按超时毫秒数）。
 * @param mode 0:超时复位  1:超时中断
 * @param rpl  复位信号保持时长倍率
 * @param ms   重装载时间（80M 下最低 0.8192ms）
 */
void ae201_wdt_init_time(uint8_t mode, uint8_t rpl, uint32_t ms)
{
	uint32_t count = ae201_clock_freq_get(AE201_CLOCK_DOMAIN_OSC80M) / 1000U;/* 1ms 的 count */

	if (((1UL << (16 + AE201_WDT_TORR_TOP_2G)) / count) < ms)
	{
		count = (1UL << (16 + AE201_WDT_TORR_TOP_2G));/* 超出可表示范围，用最大超时 */
	}
	else
	{
		count *= ms;
	}
	ae201_wdt_init_count(mode, rpl, count);
}

/* 早期默认配置（_WdogInit 使用） */
#define AE201_WDT_DEFAULT_MODE  AE201_WDT_MODE_RESET
#define AE201_WDT_DEFAULT_RPL   AE201_WDT_RPL_256
#define AE201_WDT_DEFAULT_MS    200

#if CONFIG_WDOG_INIT
/*
 * reset.S 在 z_prep_c 之前调用的 SoC hook（符号名 _WdogInit 为 arch→SoC
 * 接口约定，见文件头）。只做最小寄存器配置 + 喂狗，不触 LOG、不触全局。
 */
void _WdogInit(void)
{
	/* 先配主频（BSS 前纯 MMIO），WDT 随后按新主频算超时 count —— 保证主频
	 * 配置与 WDT 时间同步。BootROM 不强制切主频，Zephyr 需在此自行配置。 */
	ae201_clock_init();//CONFIG_WDOG_INIT没生效
	ae201_wdt_init_time(AE201_WDT_DEFAULT_MODE, AE201_WDT_DEFAULT_RPL, AE201_WDT_DEFAULT_MS);
}
#else
// #define IS_VALID_FWDGT_PRESCALER(psc)                                          \
// 	(((psc) == FWDGT_PSC_DIV4) || ((psc) == FWDGT_PSC_DIV8) ||             \
// 	 ((psc) == FWDGT_PSC_DIV16) || ((psc) == FWDGT_PSC_DIV32) ||           \
// 	 ((psc) == FWDGT_PSC_DIV64) || ((psc) == FWDGT_PSC_DIV128) ||          \
// 	 ((psc) == FWDGT_PSC_DIV256))

// #define FWDGT_INITIAL_TIMEOUT DT_INST_PROP(0, initial_timeout_ms)

// #if (FWDGT_INITIAL_TIMEOUT <= 0)
// #error Must be initial-timeout > 0
// #elif (FWDGT_INITIAL_TIMEOUT >                                                 \
// 	(FWDGT_PRESCALER_MAX * FWDGT_RELOAD_MAX * MSEC_PER_SEC /               \
// 	CONFIG_GD32_LOW_SPEED_IRC_FREQUENCY))
// #error Must be initial-timeout <= (256 * 4095 * 1000 / GD32_LOW_SPEED_IRC_FREQUENCY)
// #endif

// /**
//  * @brief Calculates FWDGT config value from timeout.
//  *
//  * @param timeout Timeout value in milliseconds.
//  * @param prescaler Pointer to the storage of prescaler value.
//  * @param reload Pointer to the storage of reload value.
//  *
//  * @return 0 on success, -EINVAL if the timeout is out of range
//  */
// static int gd32_fwdgt_calc_timeout(uint32_t timeout, uint32_t *prescaler,
// 				   uint32_t *reload)
// {
// 	uint16_t divider = 4U;
// 	uint8_t shift = 0U;
// 	uint32_t ticks = (uint64_t)CONFIG_GD32_LOW_SPEED_IRC_FREQUENCY *
// 			 timeout / MSEC_PER_SEC;

// 	while ((ticks / divider) > FWDGT_RELOAD_MAX) {
// 		shift++;
// 		divider = 4U << shift;
// 	}

// 	if (!IS_VALID_FWDGT_PRESCALER(PSC_PSC(shift)) || timeout == 0U) {
// 		return -EINVAL;
// 	}

// 	/* convert the 'shift' to prescaler value */
// 	*prescaler = PSC_PSC(shift);
// 	*reload = (ticks / divider) - 1U;

// 	return 0;
// }

// static int gd32_fwdgt_setup(const struct device *dev, uint8_t options)
// {
// 	ARG_UNUSED(dev);

// 	if ((options & WDT_OPT_PAUSE_HALTED_BY_DBG) != 0U) {
// #if CONFIG_GD32_DBG_SUPPORT
// 		dbg_periph_enable(DBG_FWDGT_HOLD);
// #else
// 		LOG_ERR("Debug support not enabled");
// 		return -ENOTSUP;
// #endif
// 	}

// 	if ((options & WDT_OPT_PAUSE_IN_SLEEP) != 0U) {
// 		LOG_ERR("WDT_OPT_PAUSE_IN_SLEEP not supported");
// 		return -ENOTSUP;
// 	}

// 	fwdgt_enable();

// 	return 0;
// }

// static int gd32_fwdgt_disable(const struct device *dev)
// {
// 	/* watchdog cannot be stopped once started */
// 	ARG_UNUSED(dev);

// 	return -EPERM;
// }

// static int gd32_fwdgt_install_timeout(const struct device *dev,
// 				      const struct wdt_timeout_cfg *config)
// {
// 	uint32_t prescaler = 0U;
// 	uint32_t reload = 0U;
// 	ErrStatus errstat = ERROR;

// 	/* Callback is not supported by FWDGT */
// 	if (config->callback != NULL) {
// 		LOG_ERR("callback not supported by FWDGT");
// 		return -ENOTSUP;
// 	}

// 	/* Calculate prescaler and reload value from timeout value */
// 	if (gd32_fwdgt_calc_timeout(config->window.max, &prescaler,
// 				    &reload) != 0) {
// 		LOG_ERR("window max is out of range");
// 		return -EINVAL;
// 	}

// 	/* Configure and run FWDGT */
// 	fwdgt_write_enable();
// 	errstat = fwdgt_config(reload, prescaler);
// 	if (errstat != SUCCESS) {
// 		LOG_ERR("fwdgt_config() failed: %d", errstat);
// 		return -EINVAL;
// 	}
// 	fwdgt_write_disable();

// 	return 0;
// }

// static int gd32_fwdgt_feed(const struct device *dev, int channel_id)
// {
// 	ARG_UNUSED(channel_id);

// 	fwdgt_counter_reload();

// 	return 0;
// }
// static const struct wdt_driver_api fwdgt_gd32_api = {
// 	.setup = gd32_fwdgt_setup,
// 	.disable = gd32_fwdgt_disable,
// 	.install_timeout = gd32_fwdgt_install_timeout,
// 	.feed = gd32_fwdgt_feed,
// };

// static int gd32_fwdgt_init(const struct device *dev)
// {
// 	int ret = 0;

// 	/* Turn on and wait stabilize system clock oscillator. */
// 	rcu_osci_on(RCU_IRC_LOW_SPEED);
// 	while (!rcu_osci_stab_wait(RCU_IRC_LOW_SPEED))
// 	{
// 	}

// #if !defined(CONFIG_WDT_DISABLE_AT_BOOT)
// 	const struct wdt_timeout_cfg config = {
// 		.window.max = FWDGT_INITIAL_TIMEOUT
// 	};

// 	ret = gd32_fwdgt_install_timeout(dev, &config);
// #endif

// 	return ret;
// }

// DEVICE_DT_INST_DEFINE(0, gd32_fwdgt_init, NULL, NULL, NULL, POST_KERNEL,
// 		      CONFIG_KERNEL_INIT_PRIORITY_DEVICE, &fwdgt_gd32_api);

//ITE
// #define LOG_LEVEL CONFIG_WDT_LOG_LEVEL
// LOG_MODULE_REGISTER(wdt_ite_it8xxx2);

// #define IT8XXX2_WATCHDOG_MAGIC_BYTE			0x5c
// #define WARNING_TIMER_PERIOD_MS_TO_1024HZ_COUNT(ms)	((ms) * 1024 / 1000)

// /* enter critical period or not */
// static int wdt_warning_fired;

// /* device config */
// struct wdt_it8xxx2_config {
// 	/* wdt register base address */
// 	struct wdt_it8xxx2_regs *base;
// };

// /* driver data */
// struct wdt_it8xxx2_data {
// 	/* timeout callback used to handle watchdog event */
// 	wdt_callback_t callback;
// 	/* indicate whether a watchdog timeout is installed */
// 	bool timeout_installed;
// 	/* watchdog feed timeout in milliseconds */
// 	uint32_t timeout;
// };

// static int wdt_it8xxx2_install_timeout(const struct device *dev,
// 					  const struct wdt_timeout_cfg *config)
// {
// 	const struct wdt_it8xxx2_config *const wdt_config = dev->config;
// 	struct wdt_it8xxx2_data *data = dev->data;
// 	struct wdt_it8xxx2_regs *const inst = wdt_config->base;

// 	/* if watchdog is already running */
// 	if ((inst->ETWCFG) & IT8XXX2_WDT_LEWDCNTL) {
// 		return -EBUSY;
// 	}

// 	/*
// 	 * Not support lower limit window timeouts (min value must be equal to
// 	 * 0). Upper limit window timeouts can't be 0 when we install timeout.
// 	 */
// 	if ((config->window.min != 0) || (config->window.max == 0)) {
// 		data->timeout_installed = false;
// 		return -EINVAL;
// 	}

// 	/* save watchdog timeout */
// 	data->timeout = config->window.max;

// 	/* install user timeout isr */
// 	data->callback = config->callback;

// 	/* mark installed */
// 	data->timeout_installed = true;

// 	return 0;
// }

// static int wdt_it8xxx2_setup(const struct device *dev, uint8_t options)
// {
// 	const struct wdt_it8xxx2_config *const wdt_config = dev->config;
// 	struct wdt_it8xxx2_data *data = dev->data;
// 	struct wdt_it8xxx2_regs *const inst = wdt_config->base;
// 	uint16_t cnt0 = WARNING_TIMER_PERIOD_MS_TO_1024HZ_COUNT(data->timeout);
// 	uint16_t cnt1 = WARNING_TIMER_PERIOD_MS_TO_1024HZ_COUNT((data->timeout
// 			+ CONFIG_WDT_ITE_WARNING_LEADING_TIME_MS));

// 	/* disable pre-warning timer1 interrupt */
// 	irq_disable(DT_INST_IRQN(0));

// 	if (!data->timeout_installed) {
// 		LOG_ERR("No valid WDT timeout installed");
// 		return -EINVAL;
// 	}

// 	if ((inst->ETWCFG) & IT8XXX2_WDT_LEWDCNTL) {
// 		LOG_ERR("WDT is already running");
// 		return -EBUSY;
// 	}

// 	if ((options & WDT_OPT_PAUSE_IN_SLEEP) != 0) {
// 		LOG_ERR("WDT_OPT_PAUSE_IN_SLEEP is not supported");
// 		return -ENOTSUP;
// 	}

// 	/* pre-warning timer1 is 16-bit counter down timer */
// 	inst->ET1CNTLHR = (cnt0 >> 8) & 0xff;
// 	inst->ET1CNTLLR = cnt0 & 0xff;

// 	/* clear pre-warning timer1 interrupt status */
// 	ite_intc_isr_clear(DT_INST_IRQN(0));

// 	/* enable pre-warning timer1 interrupt */
// 	irq_enable(DT_INST_IRQN(0));

// 	/* don't stop watchdog timer counting */
// 	inst->ETWCTRL &= ~IT8XXX2_WDT_EWDSCEN;

// 	/* set watchdog timer count */
// 	inst->EWDCNTHR = (cnt1 >> 8) & 0xff;
// 	inst->EWDCNTLR = cnt1 & 0xff;

// 	/* allow to write timer1 count register */
// 	inst->ETWCFG &= ~IT8XXX2_WDT_LET1CNTL;

// 	/*
// 	 * bit5 = 1: enable key match function to touch watchdog
// 	 * bit4 = 1: select watchdog clock source from prescaler
// 	 * bit3 = 1: lock watchdog count register (also mark as watchdog running)
// 	 * bit1 = 1: lock timer1 prescaler register
// 	 */
// 	inst->ETWCFG = (IT8XXX2_WDT_EWDKEYEN |
// 			IT8XXX2_WDT_EWDSRC |
// 			IT8XXX2_WDT_LEWDCNTL |
// 			IT8XXX2_WDT_LET1PS);

// 	LOG_DBG("WDT Setup and enabled");

// 	return 0;
// }

// /*
//  * reload the WDT and pre-warning timer1 counter
//  *
//  * @param dev Pointer to the device structure for the driver instance.
//  * @param channel_id Index of the fed channel, and we only support
//  *                   channel_id = 0 now.
//  */
// static int wdt_it8xxx2_feed(const struct device *dev, int channel_id)
// {
// 	const struct wdt_it8xxx2_config *const wdt_config = dev->config;
// 	struct wdt_it8xxx2_data *data = dev->data;
// 	struct wdt_it8xxx2_regs *const inst = wdt_config->base;
// 	uint16_t cnt0 = WARNING_TIMER_PERIOD_MS_TO_1024HZ_COUNT(data->timeout);

// 	ARG_UNUSED(channel_id);

// 	/* reset pre-warning timer1 */
// 	inst->ETWCTRL |= IT8XXX2_WDT_ET1RST;

// 	/* restart watchdog timer */
// 	inst->EWDKEYR = IT8XXX2_WATCHDOG_MAGIC_BYTE;

// 	/* reset pre-warning timer1 to default if time is touched */
// 	if (wdt_warning_fired) {
// 		wdt_warning_fired = 0;

// 		/* pre-warning timer1 is 16-bit counter down timer */
// 		inst->ET1CNTLHR = (cnt0 >> 8) & 0xff;
// 		inst->ET1CNTLLR = cnt0 & 0xff;

// 		/* clear timer1 interrupt status */
// 		ite_intc_isr_clear(DT_INST_IRQN(0));

// 		/* enable timer1 interrupt */
// 		irq_enable(DT_INST_IRQN(0));
// 	}

// 	LOG_DBG("WDT Kicking");

// 	return 0;
// }

// static int wdt_it8xxx2_disable(const struct device *dev)
// {
// 	const struct wdt_it8xxx2_config *const wdt_config = dev->config;
// 	struct wdt_it8xxx2_data *data = dev->data;
// 	struct wdt_it8xxx2_regs *const inst = wdt_config->base;

// 	/* stop watchdog timer counting */
// 	inst->ETWCTRL |= IT8XXX2_WDT_EWDSCEN;

// 	/* unlock watchdog count register (also mark as watchdog not running) */
// 	inst->ETWCFG &= ~IT8XXX2_WDT_LEWDCNTL;

// 	/* disable pre-warning timer1 interrupt */
// 	irq_disable(DT_INST_IRQN(0));

// 	/* mark uninstalled */
// 	data->timeout_installed = false;

// 	LOG_DBG("WDT Disabled");

// 	return 0;
// }

// static void wdt_it8xxx2_isr(const struct device *dev)
// {
// 	const struct wdt_it8xxx2_config *const wdt_config = dev->config;
// 	struct wdt_it8xxx2_data *data = dev->data;
// 	struct wdt_it8xxx2_regs *const inst = wdt_config->base;

// 	/* clear pre-warning timer1 interrupt status */
// 	ite_intc_isr_clear(DT_INST_IRQN(0));

// 	/* reset pre-warning timer1 */
// 	inst->ETWCTRL |= IT8XXX2_WDT_ET1RST;

// 	/* callback function, ex. print warning message */
// 	if (data->callback) {
// 		data->callback(dev, 0);
// 	}

// 	if (IS_ENABLED(CONFIG_WDT_ITE_REDUCE_WARNING_LEADING_TIME)) {
// 		/*
// 		 * Once warning timer triggered: if watchdog timer isn't reloaded,
// 		 * then we will reduce interval of warning timer to 30ms to print
// 		 * more warning messages before watchdog reset.
// 		 */
// 		if (!wdt_warning_fired) {
// 			uint16_t cnt0 = WARNING_TIMER_PERIOD_MS_TO_1024HZ_COUNT(30);

// 			/* pre-warning timer1 is 16-bit counter down timer */
// 			inst->ET1CNTLHR = (cnt0 >> 8) & 0xff;
// 			inst->ET1CNTLLR = cnt0 & 0xff;

// 			/* clear pre-warning timer1 interrupt status */
// 			ite_intc_isr_clear(DT_INST_IRQN(0));
// 		}
// 	}
// 	wdt_warning_fired++;

// 	LOG_DBG("WDT ISR");
// }

// static const struct wdt_driver_api wdt_it8xxx2_api = {
// 	.setup = wdt_it8xxx2_setup,
// 	.disable = wdt_it8xxx2_disable,
// 	.install_timeout = wdt_it8xxx2_install_timeout,
// 	.feed = wdt_it8xxx2_feed,
// };

// static int wdt_it8xxx2_init(const struct device *dev)
// {
// 	const struct wdt_it8xxx2_config *const wdt_config = dev->config;
// 	struct wdt_it8xxx2_regs *const inst = wdt_config->base;

// 	if (IS_ENABLED(CONFIG_WDT_DISABLE_AT_BOOT)) {
// 		wdt_it8xxx2_disable(dev);
// 	}

// 	/* unlock access to watchdog registers */
// 	inst->ETWCFG = 0x00;

// 	/* set WDT and timer1 to use 1.024kHz clock */
// 	inst->ET1PSR = IT8XXX2_WDT_ETPS_1P024_KHZ;

// 	/* set WDT key match enabled and WDT clock to use ET1PSR */
// 	inst->ETWCFG = (IT8XXX2_WDT_EWDKEYEN |
// 			IT8XXX2_WDT_EWDSRC);

// 	/*
// 	 * select the mode that watchdog can be stopped, this is needed for
// 	 * wdt_it8xxx2_disable() api and WDT_OPT_PAUSE_HALTED_BY_DBG flag
// 	 */
// 	inst->ETWCTRL |= IT8XXX2_WDT_EWDSCMS;

// 	IRQ_CONNECT(DT_INST_IRQN(0), 0, wdt_it8xxx2_isr,
// 		    DEVICE_DT_INST_GET(0), 0);
// 	return 0;
// }

// static const struct wdt_it8xxx2_config wdt_it8xxx2_cfg_0 = {
// 	.base = (struct wdt_it8xxx2_regs *)DT_INST_REG_ADDR(0),
// };

// static struct wdt_it8xxx2_data wdt_it8xxx2_dev_data;

// DEVICE_DT_INST_DEFINE(0, wdt_it8xxx2_init, NULL,
// 			&wdt_it8xxx2_dev_data, &wdt_it8xxx2_cfg_0,
// 			PRE_KERNEL_1, CONFIG_KERNEL_INIT_PRIORITY_DEFAULT,
// 			&wdt_it8xxx2_api);


#endif/*CONFIG_WDOG_INIT*/
#endif
