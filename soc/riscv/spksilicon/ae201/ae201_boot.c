/*
 * Copyright (c) 2026 Sparkle Silicon Technology Corp., Ltd.
 * SPDX-License-Identifier: Apache-2.0
 *
 * AE201 启动控制：可控的 secondary-core / 多镜像启动流程。
 *
 * 背景（反汇编审查发现，见 MIGRATION.md）：
 *   zephyr 通用 arch/riscv/core/reset.S 的 boot_secondary_core 在
 *   CONFIG_MP_MAX_NUM_CPUS=1 时直接 j loop_unconfigured_cores（wfi 死循环）。
 *   该路径对单核 AE201（mhartid 恒 0）实际不触发，但为多核/双镜像预留了
 *   语义不清的默认行为。
 *
 * 本文件提供一个 AE201 特化的可控函数 ae201_boot_secondary_core()，由
 * AE201_BOOT_MODE（default / dual / recovery）宏控选择流程，替代 zephyr
 * 默认 boot_secondary_core 行为。接入点：未来多核/多镜像时，让 arch 层
 * reset.S 对非引导核 call 此函数（或由 SoC 层提供自定义 reset.S）。
 */
#include <zephyr/arch/riscv/csr.h>

static void ae201_boot_default_wait(void);

#if defined(CONFIG_AE201_BOOT_DUAL)
static void ae201_boot_dual_image(void);
#endif

#if defined(CONFIG_AE201_BOOT_RECOVERY)
static void ae201_boot_recovery(void);
#endif

/**
 * @brief 非引导核的启动流程（替代 zephyr boot_secondary_core）。
 *
 * 由 AE201_BOOT_MODE 宏控分发。调用方已判定当前 hart 非引导核。
 */
void ae201_boot_secondary_core(void)
{
#if defined(CONFIG_AE201_BOOT_DUAL)
	ae201_boot_dual_image();
#elif defined(CONFIG_AE201_BOOT_RECOVERY)
	ae201_boot_recovery();
#else
	ae201_boot_default_wait();
#endif
}

/**
 * @brief 默认流程：非引导核进入低功耗 wfi 等待。
 *
 * 等同 zephyr 默认 boot_secondary_core → loop_unconfigured_cores 语义，
 * 但显式命名、可控，且不暴露 wfi 死循环为"未配置"的误导性标签。
 */
static void ae201_boot_default_wait(void)
{
	while (1) {
		__asm__ volatile("wfi");
	}
}

#if defined(CONFIG_AE201_BOOT_DUAL)
/**
 * @brief 双镜像（A/B）启动：依据 FlashInfo 的 A/B 标记选择主备镜像。
 *
 * TODO(林雨)：在此实现 A/B 选择逻辑——
 *   1. 读 FlashInfo.Fixed（0x80100 区）的 A/B 标记位；
 *   2. 校验主镜像头/校验和；
 *   3. 主镜像有效则跳主镜像，否则回退备镜像；
 *   4. 双镜像均无效时回退 ae201_boot_default_wait()。
 * 设计取舍：A/B 标记放 FlashInfo 固定区 vs 独立 flash page；
 * 跳转前是否需重新 scatterload 向量表到 0x30800。
 */
static void ae201_boot_dual_image(void)
{
	ae201_boot_default_wait();
}
#endif /* CONFIG_AE201_BOOT_DUAL */

#if defined(CONFIG_AE201_BOOT_RECOVERY)
/**
 * @brief 备份/恢复启动：主镜像损坏时从备份区恢复并启动。
 *
 * TODO(林雨)：在此实现备份恢复逻辑——
 *   1. 校验主镜像完整性；
 *   2. 损坏则从备份区拷贝恢复；
 *   3. 恢复后跳主镜像入口；
 *   4. 备份区亦无效则回退 ae201_boot_default_wait()。
 * 设计取舍：恢复触发条件（CRC / magic / 看门狗复位标志）；
 * 恢复是否需双 bank flash 擦写保护。
 */
static void ae201_boot_recovery(void)
{
	ae201_boot_default_wait();
}
#endif /* CONFIG_AE201_BOOT_RECOVERY */
