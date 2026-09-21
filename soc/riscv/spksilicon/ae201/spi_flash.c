/*
 * Copyright (c) 2026 Sparkle Silicon Technology Corp., Ltd.
 * SPDX-License-Identifier: Apache-2.0
 *
 * @file spi_flash.c
 * @brief AE201 外部 SPI NOR Flash 驱动（空函数模板）。
 *
 * 本文件是 SPI Flash 驱动的骨架。所有函数在 Flash 擦写期间执行，必须搬到
 * RAM（AE201 ICACHE 现场调度 SPIF 命中 → 取指死锁，见 configs/base.conf）。
 *
 * 落地步骤（待驱动实现后）：
 *   1. 去掉 ecfw-zephyr/configs/base.conf 里注释的 CONFIG_CODE_DATA_RELOCATION=y；
 *   2. 在 CMakeLists.txt 接入本文件并重定位到 RAM：
 *        zephyr_sources_ifdef(CONFIG_XXX spi_flash.c)
 *        zephyr_code_relocate(src/spi_flash.c RAM)   # 整文件搬到 RAM
 */

#include "spi_flash.h"

void spi_flash_write_enable(void)
{
	/* TODO(林雨)：发送 Write Enable (0x06)。 */
}

void spi_flash_wait_ready(void)
{
	/* TODO(林雨)：轮询状态寄存器 WIP 位，直到擦写完成。 */
}

void spi_flash_sector_erase(uint32_t addr)
{
	(void)addr;
	/* TODO(林雨)：发送 Sector Erase (0x20) + 地址。 */
}

void spi_flash_page_program(uint32_t addr, const uint8_t *buf, size_t len)
{
	(void)addr;
	(void)buf;
	(void)len;
	/* TODO(林雨)：发送 Page Program (0x02) + 地址 + 数据。 */
}

int spi_flash_read(uint32_t addr, uint8_t *buf, size_t len)
{
	(void)addr;
	(void)buf;
	(void)len;
	/* TODO(林雨)：发送 Read (0x03/0x0B) + 地址，读回数据。 */
	return 0;
}
