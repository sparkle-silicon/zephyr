/*
 * Copyright (c) 2026 Sparkle Silicon Technology Corp., Ltd.
 * SPDX-License-Identifier: Apache-2.0
 *
 * @file wdt.h
 * @brief AE201 看门狗（WDT）驱动头文件 —— Synopsys DW_apb_wdt 兼容。
 *
 * 命名规则（对齐固件 AE_REG.H / KERNEL_WATCHDOG.H，加 AE201_ 前缀、保留
 * 原名尾部，便于与 SPK32AE201NTO/Firmware 交叉对照）：
 *   - 寄存器偏移宏 → AE201_WDT_CR_OFFSET（原名 WDT_CR_OFFSET）
 *   - 位域/档位宏  → AE201_WDT_CR_EN / AE201_WDT_TORR_TOP_2G（原名 WDT_CR_EN / WDT_TORR_TOP_2G）
 *   - 对外函数     → ae201_wdt_*（蛇形，原名 WDT_Init / WDT_FeedDog / ...）
 */

#ifndef __RISCV_SPKSILICON_AE201_WDT_H_
#define __RISCV_SPKSILICON_AE201_WDT_H_

#include <stdint.h>
#include <zephyr/sys/util.h>   /* BIT/GENMASK/FIELD_PREP/FIELD_GET */

/* ================= 寄存器偏移（对齐 AE_REG.H，8-bit 按字节拆分） ===== */
#define AE201_WDT_BASE_ADDR        0x3C00UL

#define AE201_WDT_CR_OFFSET        0x0
#define AE201_WDT_TORR0_OFFSET     0x4   /* TOP 低字节 */
#define AE201_WDT_TORR1_OFFSET     0x5   /* TOP_INIT 高字节 */
#define AE201_WDT_CCVR0_OFFSET     0x8
#define AE201_WDT_CCVR1_OFFSET     0x9
#define AE201_WDT_CCVR2_OFFSET     0xA
#define AE201_WDT_CCVR3_OFFSET     0xB
#define AE201_WDT_CRR_OFFSET       0xC
#define AE201_WDT_STAT_OFFSET      0x10
#define AE201_WDT_EOI_OFFSET       0x14
#define AE201_WDT_OFFSET_MASK      0xFF

/* ================= CR（Control Register）位域 ======================= */
/* 单比特位 */
#define AE201_WDT_CR_EN             BIT(0)   /* 1 = 使能 WDT */
#define AE201_WDT_CR_DI             0        /* 0 = 禁用 WDT */

/* 超时响应模式（RMOD 位取值） */
#define AE201_WDT_CR_RMOD_INTR      BIT(1)   /* RMOD=1：超时先中断，二次超时复位 */
#define AE201_WDT_CR_RMOD_RESET     0        /* RMOD=0：超时直接复位 */

/* 复位脉冲长度（CR bit4:2）掩码 */
#define AE201_WDT_CR_RPL_MASK       GENMASK(4, 2)   /* = 0x1C */

/* RPL 档位（3-bit 裸值，作为 ae201_wdt_init 的 rpl 参数） */
#define AE201_WDT_RPL_2             0x0
#define AE201_WDT_RPL_4             0x1
#define AE201_WDT_RPL_8             0x2
#define AE201_WDT_RPL_16            0x3
#define AE201_WDT_RPL_32            0x4
#define AE201_WDT_RPL_64            0x5
#define AE201_WDT_RPL_128           0x6
#define AE201_WDT_RPL_256           0x7

/* RPL 在 CR 中的已移位值（FIELD_PREP 由裸值生成，与固件
 * WDT_CR_RPL_2..256 的 0x00/0x04/.../0x1C 一一对应） */
#define AE201_WDT_CR_RPL_2          FIELD_PREP(AE201_WDT_CR_RPL_MASK, AE201_WDT_RPL_2)    /* = 0x00 */
#define AE201_WDT_CR_RPL_4          FIELD_PREP(AE201_WDT_CR_RPL_MASK, AE201_WDT_RPL_4)    /* = 0x04 */
#define AE201_WDT_CR_RPL_8          FIELD_PREP(AE201_WDT_CR_RPL_MASK, AE201_WDT_RPL_8)    /* = 0x08 */
#define AE201_WDT_CR_RPL_16         FIELD_PREP(AE201_WDT_CR_RPL_MASK, AE201_WDT_RPL_16)   /* = 0x0C */
#define AE201_WDT_CR_RPL_32         FIELD_PREP(AE201_WDT_CR_RPL_MASK, AE201_WDT_RPL_32)   /* = 0x10 */
#define AE201_WDT_CR_RPL_64         FIELD_PREP(AE201_WDT_CR_RPL_MASK, AE201_WDT_RPL_64)   /* = 0x14 */
#define AE201_WDT_CR_RPL_128        FIELD_PREP(AE201_WDT_CR_RPL_MASK, AE201_WDT_RPL_128)  /* = 0x18 */
#define AE201_WDT_CR_RPL_256        FIELD_PREP(AE201_WDT_CR_RPL_MASK, AE201_WDT_RPL_256)  /* = 0x1C */

/* ================= TORR（Timeout Range Register）位域 =============== */
/* timeout = 2^(16 + TOP) 个 pclk_wdt 时钟，TOP 取值 0..15 */
#define AE201_WDT_TORR_TOP_MASK      GENMASK(3, 0)   /* = 0x0F */
#define AE201_WDT_TORR_TOP_INIT_MASK GENMASK(7, 4)   /* = 0xF0 */

/* TOP 档位（含具体超时时长） */
#define AE201_WDT_TORR_TOP_64K       0x0    /* 2^16 时钟 */
#define AE201_WDT_TORR_TOP_128K      0x1    /* 2^17 时钟 */
#define AE201_WDT_TORR_TOP_256K      0x2
#define AE201_WDT_TORR_TOP_512K      0x3
#define AE201_WDT_TORR_TOP_1M        0x4
#define AE201_WDT_TORR_TOP_2M        0x5
#define AE201_WDT_TORR_TOP_4M        0x6
#define AE201_WDT_TORR_TOP_8M        0x7
#define AE201_WDT_TORR_TOP_16M       0x8
#define AE201_WDT_TORR_TOP_32M       0x9
#define AE201_WDT_TORR_TOP_64M       0xA
#define AE201_WDT_TORR_TOP_128M      0xB
#define AE201_WDT_TORR_TOP_256M      0xC
#define AE201_WDT_TORR_TOP_512M      0xD
#define AE201_WDT_TORR_TOP_1G        0xE
#define AE201_WDT_TORR_TOP_2G        0xF    /* 2^31 时钟 */

/* ================= CCVR / CRR / STAT / EOI 位域 ===================== */
#define AE201_WDT_CCVR0_MASK        0xFF
#define AE201_WDT_CCVR1_MASK        0xFF
#define AE201_WDT_CCVR2_MASK        0xFF
#define AE201_WDT_CCVR3_MASK        0xFF

#define AE201_WDT_CRR_CRR           0x76          /* 写此魔数喂狗（重载计数器，同时清中断） */
#define AE201_WDT_STAT_ISR          BIT(0)        /* 1 = 超时中断激活 */
#define AE201_WDT_EOI_EOI           BIT(0)        /* 读 EOI 即清中断 */

/* ================= ae201_wdt_init 的 mode 参数 ====================== */
#define AE201_WDT_MODE_INTR         0x01          /* 超时先中断 */
#define AE201_WDT_MODE_RESET        0x00          /* 超时直接复位 */

/* ================= 常量 ============================================= */
/* 喂狗失败重试上限（防止 WDT 异常时无限循环死锁） */
#define AE201_WDT_FEEDDOG_TIMEOUT   100

/* ================= 对外 API（蛇形） ================================= */

/**
 * @brief 看门狗初始化（按超时档位）。
 * @param mode 0:超时复位  1:超时中断（AE201_WDT_MODE_RESET / _INTR）
 * @param rpl  复位脉冲长度（AE201_WDT_RPL_2..AE201_WDT_RPL_256）
 * @param top  超时档位（AE201_WDT_TORR_TOP_64K..AE201_WDT_TORR_TOP_2G）
 */
void ae201_wdt_init(uint8_t mode, uint8_t rpl, uint8_t top);

/**
 * @brief 看门狗初始化（按重装载计数值）。
 * @param count pclk_wdt 计数值。
 */
void ae201_wdt_init_count(uint8_t mode, uint8_t rpl, uint32_t count);

/**
 * @brief 看门狗初始化（按超时毫秒数）。
 * @param ms 重装载时间（80M 下最低 0.8192ms）。
 */
void ae201_wdt_init_time(uint8_t mode, uint8_t rpl, uint32_t ms);

/** @brief 喂狗（重载计数器，同时清中断）。 */
void ae201_wdt_feed(void);

/** @brief 清除看门狗超时中断，但不喂狗。 */
void ae201_wdt_clear_irq(void);

/*
 * reset.S 在 z_prep_c 之前调用的 SoC hook（符号名 _WdogInit 为 arch→SoC
 * 接口约定，不可改）。内部走 ae201_wdt_init_time 的最小配置 + 喂狗。
 */
void _WdogInit(void);

#endif /* __RISCV_SPKSILICON_AE201_WDT_H_ */
