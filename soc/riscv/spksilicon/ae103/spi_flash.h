/*
 * Copyright (c) 2026 Sparkle Silicon Technology Corp., Ltd.
 * SPDX-License-Identifier: Apache-2.0
 *
 * @file spi_flash.h
 * @brief AE103 外部 SPI NOR Flash 驱动接口（模板）。
 *
 * ⚠️ 重定位约束：下列函数在 Flash 擦写期间被执行。因 AE103 ICACHE 采用
 *    「现场调度 SPIF 命中」机制（见 ecfw-zephyr/configs/base.conf），擦写时
 *    若这些函数仍在 Flash 中 XIP 执行会造成取指死锁，故整个 spi_flash.c
 *    必须用 zephyr_code_relocate() 搬到 RAM 运行。
 */

#ifndef ZEPHYR_SOC_RISCV_SPKSILICON_AE103_SPI_FLASH_H_
#define ZEPHYR_SOC_RISCV_SPKSILICON_AE103_SPI_FLASH_H_

#include <stdint.h>
#include <stddef.h>

/** 写使能（Write Enable, 0x06）*/
void spi_flash_write_enable(void);

/** 等待擦写完成（轮询状态寄存器 WIP 位）*/
void spi_flash_wait_ready(void);

/** 扇区擦除（Sector Erase, 0x20）*/
void spi_flash_sector_erase(uint32_t addr);

/** 页编程（Page Program, 0x02）*/
void spi_flash_page_program(uint32_t addr, const uint8_t *buf, size_t len);

/** 读数据（Read, 0x03/0x0B）*/
int spi_flash_read(uint32_t addr, uint8_t *buf, size_t len);

#endif /* ZEPHYR_SOC_RISCV_SPKSILICON_AE103_SPI_FLASH_H_ */
