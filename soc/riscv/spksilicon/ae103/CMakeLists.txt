# SPDX-License-Identifier: Apache-2.0

zephyr_sources(soc.c)

# 可控 secondary-core / 多镜像启动流程（AE103_BOOT_MODE 宏控）。
zephyr_sources(ae103_boot.c)

# 早期看门狗初始化（reset.S call _WdogInit，z_prep_c 之前）。
zephyr_sources_ifdef(CONFIG_WDOG_INIT wdt.c)

zephyr_sources_ifdef(CONFIG_AE103_TIMER timer.c)

zephyr_sources_ifdef(CONFIG_AE103_UART uart.c)

# SPI Flash 驱动（空模板）。AE103 ICACHE 采用「现场调度 SPIF 命中」机制，
# Flash 擦写期间驱动若仍 XIP 执行会取指死锁，故整文件搬 RAM。
zephyr_sources_ifdef(CONFIG_AE103_SPI_FLASH spi_flash.c)
zephyr_code_relocate_ifdef(CONFIG_AE103_SPI_FLASH FILES spi_flash.c LOCATION RAM)

# Reuse the generic RISC-V linker script. RAM/ROM base addresses and sizes
# are derived from the /chosen/zephyr,sram and /chosen/zephyr,flash nodes.
set(SOC_LINKER_SCRIPT ${ZEPHYR_BASE}/include/zephyr/arch/riscv/common/linker.ld
    CACHE INTERNAL "SoC Linker script ${SOC_NAME}")
