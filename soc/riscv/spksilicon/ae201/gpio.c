/*
 * Copyright (c) 2026 Sparkle Silicon Technology Corp., Ltd.
 * SPDX-License-Identifier: Apache-2.0
 *
 * @file gpio.c
 * @brief AE201 GPIO 驱动实现 —— DW_apb_gpio 兼容。
 *
 * 寄存器/端口拓扑见 gpio.h 文件头。实现要点：
 *   - 寄存器为 8-bit 字节粒度（对齐 AE_REG.H 的 REG8），按 pin/8 分组访问。
 *   - 5 个逻辑端口（GPIOA~E）经 gpio_port_map 表映射到物理基址 + 各组偏移，
 *     收敛固件 KERNEL_GPIO.c 里 5 段重复 switch。
 *   - 引脚复用写 SYSCTL PIOx_CFG（对齐固件 sysctl_iomux_config 的 GPIO 映射）。
 *   - 中断相关（zephyr pin_interrupt_configure）留 -ENOTSUP，待 INTC 驱动落地。
 */

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/gpio/gpio_utils.h>
#include <zephyr/sys/sys_io.h>
#include <zephyr/sys/util.h>

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(ae201_gpio, LOG_LEVEL_INF);

#include "gpio.h"
#include "sysctl.h"

#define DT_DRV_COMPAT spksilicon_ae201_gpio

/* ================= 端口映射表（对齐固件 GPIOA~E） ================== */
struct ae201_gpio_port_map {
	uint32_t base;    /* 物理基址 */
	uint32_t dr_off;  /* 数据（输出值）寄存器组起始偏移 */
	uint32_t ddr_off; /* 方向寄存器组起始偏移 */
	uint32_t ext_off; /* 输入数据寄存器组起始偏移 */
	uint8_t  ngpios;  /* pin 数 */
};

static const struct ae201_gpio_port_map gpio_port_map[AE201_GPIO_PORT_NUM] = {
	[AE201_GPIO_PORTA] = { 0x2800UL, AE201_GPIO_DR0_OFFSET, AE201_GPIO_DDR0_OFFSET,
			       AE201_GPIO_EXT0_OFFSET, AE201_GPIO_NUM_PORTA },
	[AE201_GPIO_PORTB] = { 0x2C00UL, AE201_GPIO_DR0_OFFSET, AE201_GPIO_DDR0_OFFSET,
			       AE201_GPIO_EXT0_OFFSET, AE201_GPIO_NUM_PORTB },
	[AE201_GPIO_PORTC] = { 0x3000UL, AE201_GPIO_DR0_OFFSET, AE201_GPIO_DDR0_OFFSET,
			       AE201_GPIO_EXT0_OFFSET, AE201_GPIO_NUM_PORTC },
	/* GPIOD 挂在 0x3000 高位，寄存器偏移非标准（GPIO2_DR2 等） */
	[AE201_GPIO_PORTD] = { 0x3000UL, AE201_GPIO2_DR2_OFFSET, AE201_GPIO2_DDR2_OFFSET,
			       AE201_GPIO2_EXT2_OFFSET, AE201_GPIO_NUM_PORTD },
	[AE201_GPIO_PORTE] = { 0x3400UL, AE201_GPIO_DR0_OFFSET, AE201_GPIO_DDR0_OFFSET,
			       AE201_GPIO_EXT0_OFFSET, AE201_GPIO_NUM_PORTE },
};

/* ================= 寄存器访问（字节粒度） ========================== */
static inline uint32_t gpio_port_read(const struct ae201_gpio_port_map *m, uint32_t reg_off)
{
	uint32_t val = 0U;
	uint32_t nbytes = (m->ngpios + 7U) / 8U;

	for (uint32_t i = 0U; i < nbytes; i++) {
		val |= (uint32_t)sys_read8(m->base + reg_off + i) << (i * 8U);
	}
	return val;
}

static inline void gpio_port_write(const struct ae201_gpio_port_map *m, uint32_t reg_off,
				   uint32_t val)
{
	uint32_t nbytes = (m->ngpios + 7U) / 8U;

	for (uint32_t i = 0U; i < nbytes; i++) {
		sys_write8((uint8_t)(val >> (i * 8U)), m->base + reg_off + i);
	}
}

/* ================= 引脚复用（写 SYSCTL PIOx_CFG，复用值 0 = GPIO） = */
static void gpio_pin_mux_gpio(uint32_t port, uint32_t pin)
{
	if (port <= AE201_GPIO_PORTC) {
		/* GPIOA/B/C → PIO0/1 或 PIO2/3 或 PIO4，每 16 pin 换一个 PIO */
		uint32_t pio = port * 2U + ((pin >= 16U) ? 1U : 0U);

		ae201_sysctl_pio_cfg_set(pio, pin & 0xFU, 0U);
	} else if (port == AE201_GPIO_PORTD) {
		ae201_sysctl_pio_cfg_set(5U, pin, 0U);
	} else { /* GPIOE：对齐固件 sysctl_iomux_config 的特殊偏移 */
		if ((pin >= 10U) && (pin <= 13U)) {
			ae201_sysctl_pio_cfg_set(5U, pin, 0U);
		} else if ((pin >= 22U) && (pin <= 23U)) {
			ae201_sysctl_pio_cfg_set(5U, pin - 8U, 0U);
		} else {
			ae201_sysctl_pio_cfg_set(5U, 9U, 0U);
		}
	}
}

/* 逻辑端口 → SYSCTL PIO 编号 + 端口内位（GPIOD 用 PIO2 高位 +16） */
static void gpio_pio_select(uint32_t port, uint32_t pin, uint32_t *pio, uint32_t *p)
{
	switch (port) {
	case AE201_GPIO_PORTA:
		*pio = 0U; *p = pin; break;
	case AE201_GPIO_PORTB:
		*pio = 1U; *p = pin; break;
	case AE201_GPIO_PORTC:
		*pio = 2U; *p = pin; break;
	case AE201_GPIO_PORTD:
		*pio = 2U; *p = pin + 16U; break;
	case AE201_GPIO_PORTE:
	default:
		*pio = 3U; *p = pin; break;
	}
}

/* ================= 裸机 API（对齐固件 KERNEL_GPIO.c） ============== */
int ae201_gpio_config(uint32_t port, uint32_t pin, uint32_t mode,
		      uint32_t op_val, uint32_t int_lv, uint32_t pol)
{
	if (port >= AE201_GPIO_PORT_NUM) {
		return -1;
	}
	const struct ae201_gpio_port_map *m = &gpio_port_map[port];

	if (pin >= m->ngpios) {
		return -1;
	}

	/* 复用为 GPIO（对齐固件 GPIO_Config 的 sysctl_iomux_config(..., 0)） */
	gpio_pin_mux_gpio(port, pin);

	uint8_t byte = (uint8_t)(pin / 8U);
	uint8_t bit = (uint8_t)(pin % 8U);
	uint32_t dr = m->base + m->dr_off + byte;
	uint32_t ddr = m->base + m->ddr_off + byte;
	uint32_t inten = m->base + AE201_GPIO_INTEN0_OFFSET + byte;

	if (mode == AE201_GPIO_MODE_OUTPUT) {
		if (op_val != 0U) {
			sys_write8(sys_read8(dr) | BIT(bit), dr);
		} else {
			sys_write8(sys_read8(dr) & ~BIT(bit), dr);
		}
		sys_write8(sys_read8(ddr) | BIT(bit), ddr);        /* 输出 */
		sys_write8(sys_read8(inten) & ~BIT(bit), inten);   /* 非中断 */
	} else if (mode == AE201_GPIO_MODE_INPUT) {
		sys_write8(sys_read8(ddr) & ~BIT(bit), ddr);       /* 输入 */
		sys_write8(sys_read8(inten) & ~BIT(bit), inten);   /* 非中断 */
	} else { /* 中断模式（寄存器配置完整，INTC 衔接留待 INTC 驱动） */
		uint32_t intmask = m->base + AE201_GPIO_INTMASK0_OFFSET + byte;
		uint32_t inttype = m->base + AE201_GPIO_INTTYPE_LEVEL0_OFFSET + byte;
		uint32_t polarity = m->base + AE201_GPIO_INT_POLARITY0_OFFSET + byte;
		uint32_t debounce = m->base + AE201_GPIO_DEBOUNCE0_OFFSET + byte;

		sys_write8(sys_read8(ddr) & ~BIT(bit), ddr);                    /* 输入 */
		sys_write8(sys_read8(intmask) & ~BIT(bit), intmask);            /* 解除屏蔽 */
		/* 触发方式/极性：读改写（固件原版直接赋值会覆盖同字节其他 pin） */
		sys_write8((sys_read8(inttype) & ~BIT(bit)) | (int_lv ? BIT(bit) : 0U), inttype);
		sys_write8((sys_read8(polarity) & ~BIT(bit)) | (pol ? BIT(bit) : 0U), polarity);
		sys_write8(sys_read8(debounce) | BIT(bit), debounce);           /* 防抖 */
		sys_write8(sys_read8(inten) | BIT(bit), inten);                 /* 使能中断 */
	}
	return 0;
}

void ae201_gpio_input_enable(uint32_t port, uint32_t pin, bool enable)
{
	if (port >= AE201_GPIO_PORT_NUM || pin >= gpio_port_map[port].ngpios) {
		return;
	}
	uint32_t pio, p;

	gpio_pio_select(port, pin, &pio, &p);
	uint32_t off = AE201_SYSCTL_PIO0_IECFG_OFFSET + pio * 4U;
	uint32_t val = sys_read32(AE201_SYSCTL_BASE_ADDR + off);

	if (enable) {
		val |= BIT(p);
	} else {
		val &= ~BIT(p);
	}
	sys_write32(val, AE201_SYSCTL_BASE_ADDR + off);
}

void ae201_gpio_pullup_config(uint32_t port, uint32_t pin, bool enable)
{
	if (port >= AE201_GPIO_PORT_NUM || pin >= gpio_port_map[port].ngpios) {
		return;
	}
	uint32_t pio, p;

	gpio_pio_select(port, pin, &pio, &p);
	uint32_t off = AE201_SYSCTL_PIO0_UDCFG_OFFSET + pio * 4U;
	uint32_t val = sys_read32(AE201_SYSCTL_BASE_ADDR + off);

	if (enable) {
		val |= BIT(p);
	} else {
		val &= ~BIT(p);
	}
	sys_write32(val, AE201_SYSCTL_BASE_ADDR + off);
}

/* 1.8V IO 的 pin→pad 映射表（照搬固件 GPIO_1V8 的 switch） */
static const struct {
	uint32_t port;
	uint32_t pin;
	uint8_t  pad;
} gpio_1v8_map[] = {
	{ AE201_GPIO_PORTA, 14, 0 }, { AE201_GPIO_PORTA, 28, 1 },
	{ AE201_GPIO_PORTA, 24, 4 }, { AE201_GPIO_PORTA, 25, 7 },
	{ AE201_GPIO_PORTA, 26, 8 }, { AE201_GPIO_PORTA, 27, 9 },
	{ AE201_GPIO_PORTA, 4, 10 }, { AE201_GPIO_PORTA, 5, 11 },
	{ AE201_GPIO_PORTA, 17, 19 }, { AE201_GPIO_PORTA, 18, 20 },
	{ AE201_GPIO_PORTA, 16, 23 }, { AE201_GPIO_PORTA, 13, 25 },
	{ AE201_GPIO_PORTB, 6, 3 }, { AE201_GPIO_PORTB, 10, 12 },
	{ AE201_GPIO_PORTB, 11, 13 }, { AE201_GPIO_PORTB, 12, 14 },
	{ AE201_GPIO_PORTB, 13, 15 }, { AE201_GPIO_PORTB, 20, 16 },
	{ AE201_GPIO_PORTB, 21, 17 }, { AE201_GPIO_PORTB, 22, 18 },
	{ AE201_GPIO_PORTB, 14, 21 }, { AE201_GPIO_PORTB, 15, 22 },
	{ AE201_GPIO_PORTD, 8, 24 },
};

void ae201_gpio_1v8(uint32_t port, uint32_t pin, bool enable)
{
	for (uint32_t i = 0U; i < ARRAY_SIZE(gpio_1v8_map); i++) {
		if (gpio_1v8_map[i].port == port && gpio_1v8_map[i].pin == pin) {
			uint32_t off = AE201_SYSCTL_PAD_1P8_OFFSET;
			uint32_t val = sys_read32(AE201_SYSCTL_BASE_ADDR + off);

			if (enable) {
				val |= BIT(gpio_1v8_map[i].pad);
			} else {
				val &= ~BIT(gpio_1v8_map[i].pad);
			}
			sys_write32(val, AE201_SYSCTL_BASE_ADDR + off);
			return;
		}
	}
	/* 未匹配：该 pin 不支持 1.8V，无操作（固件用 offset=31 哨兵，此处等价且更安全） */
}

/* ================= zephyr gpio_driver_api ========================== */
struct ae201_gpio_config {
	struct gpio_driver_config common;
	uint8_t port; /* 逻辑端口（gpio_port_map 下标） */
};

static int ae201_gpio_pin_configure(const struct device *dev, gpio_pin_t pin, gpio_flags_t flags)
{
	const struct ae201_gpio_config *cfg = dev->config;
	const struct ae201_gpio_port_map *m = &gpio_port_map[cfg->port];

	if (pin >= m->ngpios) {
		return -EINVAL;
	}
	if ((flags & GPIO_INT_ENABLE) != 0U) {
		return -ENOTSUP; /* 中断待 INTC 驱动落地 */
	}
	if ((flags & GPIO_PULL_DOWN) != 0U) {
		return -ENOTSUP; /* AE201 仅上拉（PIOx_UDCFG） */
	}

	gpio_pin_mux_gpio(cfg->port, pin);

	uint8_t byte = (uint8_t)(pin / 8U);
	uint8_t bit = (uint8_t)(pin % 8U);
	uint32_t dr = m->base + m->dr_off + byte;
	uint32_t ddr = m->base + m->ddr_off + byte;
	uint32_t inten = m->base + AE201_GPIO_INTEN0_OFFSET + byte;

	if ((flags & GPIO_OUTPUT) != 0U) {
		if ((flags & GPIO_OUTPUT_INIT_HIGH) != 0U) {
			sys_write8(sys_read8(dr) | BIT(bit), dr);
		} else if ((flags & GPIO_OUTPUT_INIT_LOW) != 0U) {
			sys_write8(sys_read8(dr) & ~BIT(bit), dr);
		}
		sys_write8(sys_read8(ddr) | BIT(bit), ddr);
		sys_write8(sys_read8(inten) & ~BIT(bit), inten);
	} else {
		sys_write8(sys_read8(ddr) & ~BIT(bit), ddr);
		sys_write8(sys_read8(inten) & ~BIT(bit), inten);
		ae201_gpio_input_enable(cfg->port, pin, true);
	}

	if ((flags & GPIO_PULL_UP) != 0U) {
		ae201_gpio_pullup_config(cfg->port, pin, true);
	}
	return 0;
}

static int ae201_gpio_port_get_raw(const struct device *dev, gpio_port_value_t *value)
{
	const struct ae201_gpio_config *cfg = dev->config;
	const struct ae201_gpio_port_map *m = &gpio_port_map[cfg->port];

	*value = gpio_port_read(m, m->ext_off);
	return 0;
}

static int ae201_gpio_port_set_masked_raw(const struct device *dev,
					  gpio_port_pins_t mask, gpio_port_value_t value)
{
	const struct ae201_gpio_config *cfg = dev->config;
	const struct ae201_gpio_port_map *m = &gpio_port_map[cfg->port];

	gpio_port_write(m, m->dr_off, (gpio_port_read(m, m->dr_off) & ~mask) | (value & mask));
	return 0;
}

static int ae201_gpio_port_set_bits_raw(const struct device *dev, gpio_port_pins_t mask)
{
	const struct ae201_gpio_config *cfg = dev->config;
	const struct ae201_gpio_port_map *m = &gpio_port_map[cfg->port];

	gpio_port_write(m, m->dr_off, gpio_port_read(m, m->dr_off) | mask);
	return 0;
}

static int ae201_gpio_port_clear_bits_raw(const struct device *dev, gpio_port_pins_t mask)
{
	const struct ae201_gpio_config *cfg = dev->config;
	const struct ae201_gpio_port_map *m = &gpio_port_map[cfg->port];

	gpio_port_write(m, m->dr_off, gpio_port_read(m, m->dr_off) & ~mask);
	return 0;
}

static int ae201_gpio_port_toggle_bits(const struct device *dev, gpio_port_pins_t mask)
{
	const struct ae201_gpio_config *cfg = dev->config;
	const struct ae201_gpio_port_map *m = &gpio_port_map[cfg->port];

	gpio_port_write(m, m->dr_off, gpio_port_read(m, m->dr_off) ^ mask);
	return 0;
}

static int ae201_gpio_pin_interrupt_configure(const struct device *dev, gpio_pin_t pin,
					      enum gpio_int_mode mode, enum gpio_int_trig trig)
{
	ARG_UNUSED(dev);
	ARG_UNUSED(pin);
	ARG_UNUSED(mode);
	ARG_UNUSED(trig);
	return -ENOTSUP; /* TODO: 待 INTC 驱动落地后补 */
}

static int ae201_gpio_init(const struct device *dev)
{
	ARG_UNUSED(dev);
	/* 使能 GPIO 主时钟 + 防抖时钟（对齐固件 GPIO_CLOCK_EN / GPIODB_CLOCK_EN）。
	 * 幂等，多实例重复调用无害。 */
	ae201_sysctl_clock_enable(AE201_SYSCTL_MODEN0_GPIO_EN,
				  AE201_SYSCTL_MODEN1_GPIODB_EN);
	return 0;
}

static const struct gpio_driver_api gpio_api = {
	.pin_configure = ae201_gpio_pin_configure,
	.port_get_raw = ae201_gpio_port_get_raw,
	.port_set_masked_raw = ae201_gpio_port_set_masked_raw,
	.port_set_bits_raw = ae201_gpio_port_set_bits_raw,
	.port_clear_bits_raw = ae201_gpio_port_clear_bits_raw,
	.port_toggle_bits = ae201_gpio_port_toggle_bits,
	.pin_interrupt_configure = ae201_gpio_pin_interrupt_configure,
};

#define AE201_GPIO_INIT(n)                                              \
	static const struct ae201_gpio_config gpio_config_##n = {       \
		.common = {                                             \
			.port_pin_mask = GPIO_PORT_PIN_MASK_FROM_NGPIOS( \
				DT_INST_PROP(n, ngpios)),              \
		},                                                      \
		.port = DT_INST_PROP(n, spksilicon_port),              \
	};                                                              \
	DEVICE_DT_INST_DEFINE(n, ae201_gpio_init, NULL, NULL,          \
			      &gpio_config_##n, POST_KERNEL,            \
			      CONFIG_GPIO_INIT_PRIORITY, &gpio_api);

DT_INST_FOREACH_STATUS_OKAY(AE201_GPIO_INIT)
