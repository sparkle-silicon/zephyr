/*
 * Copyright (c) 2026 Sparkle Silicon Technology Corp., Ltd.
 * SPDX-License-Identifier: Apache-2.0
 *
 * @file gpio.h
 * @brief AE201 GPIO 驱动 —— Synopsys DesignWare DW_apb_gpio 兼容。
 *
 * 命名规则（对齐固件 AE_REG.H / KERNEL_GPIO.c，加 AE201_ 前缀、保留原名尾部）：
 *   - 寄存器偏移宏 → AE201_GPIO_DR0_OFFSET（原名 GPIO_DR0_OFFSET）
 *   - 对外函数     → ae201_gpio_*（蛇形）
 *
 * 硬件拓扑（5 个逻辑端口 → 4 个物理基址，对齐 KERNEL_GPIO.H 的 GPIOA~E）：
 *   GPIOA(32 pin) → 0x2800    GPIOB(32 pin) → 0x2C00
 *   GPIOC(16 pin) → 0x3000（标准偏移）    GPIOD(9 pin) → 0x3000（高位偏移 GPIO2_DR2）
 *   GPIOE(24 pin) → 0x3400
 * 其中 GPIOC 与 GPIOD 共享 0x3000 基址，GPIOD 用高位寄存器
 * （DR2@0x0C / DDR2@0x10 / EXT2@0x54），是 AE201 特有布局。
 */

#ifndef __RISCV_SPKSILICON_AE201_GPIO_H_
#define __RISCV_SPKSILICON_AE201_GPIO_H_

#include <stdint.h>
#include <stdbool.h>

/* ================= 寄存器偏移（对齐 AE_REG.H:598-665） ============== */
/* 标准寄存器组（GPIOA/B/C/E 用；字节粒度，DR1~3 = DR0 +1~+3，余同） */
#define AE201_GPIO_DR0_OFFSET             0x00 /* 数据（输出值） */
#define AE201_GPIO_DDR0_OFFSET            0x04 /* 数据方向（1=输出 0=输入） */
#define AE201_GPIO_INTEN0_OFFSET          0x30 /* 中断使能 */
#define AE201_GPIO_INTMASK0_OFFSET        0x34 /* 中断屏蔽 */
#define AE201_GPIO_INTTYPE_LEVEL0_OFFSET  0x38 /* 触发方式（0=电平 1=边沿） */
#define AE201_GPIO_INT_POLARITY0_OFFSET   0x3C /* 触发极性（1=上升/高 0=下降/低） */
#define AE201_GPIO_INTSTATUS0_OFFSET      0x40 /* 中断状态 */
#define AE201_GPIO_RAW_INTSTATUS0_OFFSET  0x44 /* 原始中断状态 */
#define AE201_GPIO_DEBOUNCE0_OFFSET       0x48 /* 防抖使能 */
#define AE201_GPIO_EOI0_OFFSET            0x4C /* 中断清除 */
#define AE201_GPIO_EXT0_OFFSET            0x50 /* 输入数据（引脚实际电平） */

/* GPIO2 特殊（GPIOD 用，挂在 0x3000 基址高位，非标准偏移） */
#define AE201_GPIO2_DR2_OFFSET            0x0C
#define AE201_GPIO2_DR3_OFFSET            0x0D
#define AE201_GPIO2_DDR2_OFFSET           0x10
#define AE201_GPIO2_DDR3_OFFSET           0x11
#define AE201_GPIO2_EXT2_OFFSET           0x54
#define AE201_GPIO2_EXT3_OFFSET           0x55

/* ================= 逻辑端口枚举（对齐 KERNEL_GPIO.H GPIOA~E） ======= */
enum ae201_gpio_port {
	AE201_GPIO_PORTA = 0,
	AE201_GPIO_PORTB,
	AE201_GPIO_PORTC,
	AE201_GPIO_PORTD,
	AE201_GPIO_PORTE,
	AE201_GPIO_PORT_NUM,
};

/* ================= 端口 pin 数（对齐 KERNEL_GPIO.H NUM_OF_GPIOx） ==== */
#define AE201_GPIO_NUM_PORTA 32U
#define AE201_GPIO_NUM_PORTB 32U
#define AE201_GPIO_NUM_PORTC 16U
#define AE201_GPIO_NUM_PORTD 9U
#define AE201_GPIO_NUM_PORTE 24U

/* ================= 引脚配置模式（对齐 GPIO_Config 的 mode） ========= */
#define AE201_GPIO_MODE_INPUT  0U
#define AE201_GPIO_MODE_OUTPUT 1U
#define AE201_GPIO_MODE_INT    2U

/* ================= 对外裸机 API（对齐固件 KERNEL_GPIO.c） =========== */

/**
 * @brief 配置单个 GPIO 引脚（对齐固件 GPIO_Config）。
 * @param port  逻辑端口（AE201_GPIO_PORTA~E）。
 * @param pin   端口内引脚号（0..AE201_GPIO_NUM_PORTx-1）。
 * @param mode  模式：AE201_GPIO_MODE_INPUT / OUTPUT / INT。
 * @param op_val 输出模式下初值（1=高 0=低），其他模式忽略。
 * @param int_lv 中断模式下触发方式（0=电平 1=边沿），其他模式忽略。
 * @param pol    中断模式下触发极性（1=上升/高 0=下降/低），其他模式忽略。
 * @return 0 成功，-1 失败（pin 越界）。
 */
int ae201_gpio_config(uint32_t port, uint32_t pin, uint32_t mode,
		      uint32_t op_val, uint32_t int_lv, uint32_t pol);

/**
 * @brief 输入使能开关（对齐固件 GPIO_Input_EN，写 SYSCTL PIOx_IECFG）。
 *        引脚不作为输入时不使能可进一步降低功耗。
 */
void ae201_gpio_input_enable(uint32_t port, uint32_t pin, bool enable);

/**
 * @brief 上拉配置（对齐固件 GPIO_Pullup_Config，写 SYSCTL PIOx_UDCFG）。
 *        固件原版仅支持使能，此处扩展 disable（清位）以对称 input_enable。
 */
void ae201_gpio_pullup_config(uint32_t port, uint32_t pin, bool enable);

/**
 * @brief 1.8V IO 电平配置（对齐固件 GPIO_1V8，写 SYSCTL PAD_1P8）。
 *        仅特定 pad 支持 1.8V，pin→pad 映射见 gpio.c 内表（照搬固件）。
 */
void ae201_gpio_1v8(uint32_t port, uint32_t pin, bool enable);

#endif /* __RISCV_SPKSILICON_AE201_GPIO_H_ */
