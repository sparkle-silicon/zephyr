# SPDX-License-Identifier: Apache-2.0

# AE201 平台特有编译选项（RISC-V 特有 flag 归 SoC 层，应用框架 ecfw-zephyr 不承载）。
# 开关值在 configs/opt.conf，Kconfig 定义见本目录 Kconfig.soc 的
# "AE201 compiler options" menu。
if(CONFIG_AE201_OPT_FSIGNED_CHAR)
  zephyr_compile_options(-fsigned-char)
endif()
if(CONFIG_AE201_OPT_MSAVE_RESTORE)
  zephyr_compile_options(-msave-restore)
endif()
if(CONFIG_AE201_OPT_FNO_UNROLL_LOOPS)
  zephyr_compile_options(-fno-unroll-loops)
endif()
if(CONFIG_AE201_OPT_WEXTRA)
  zephyr_compile_options(-Wextra)
endif()

zephyr_sources(soc.c)

# 基础模块 SYSCTL（时钟门控/复位/分频/引脚复用）已迁出本目录，改由父仓库
# ecfw-library/spksilicon/spk32ae/ae201/{ll,sl,hal}/ 提供，编译登记在
# configs/spksilicon/spk32ae/ae201/CMakeLists.txt。本目录不再持有其源文件；
# 引用方（soc.c / wdt.c / gpio.c / clock.c / uart.c）现通过 ae201_sysctl_sl.h 使用。

# 时钟域管理：主频单一事实源 + 频率拉取 + 变更通知。wdt.c/uart.c 依赖
# ae201_clock_freq_get（早期路径纯 MMIO），与已迁出的 SYSCTL 同属「无条件
# 编译」之列，故这里不设 Kconfig 开关。
zephyr_sources(clock.c)

# 可控 secondary-core / 多镜像启动流程（AE201_BOOT_MODE 宏控）。
zephyr_sources(ae201_boot.c)

# 早期看门狗初始化（reset.S call _WdogInit，z_prep_c 之前）。
zephyr_sources_ifdef(CONFIG_WDOG_INIT wdt.c)

zephyr_sources_ifdef(CONFIG_AE201_TIMER timer.c)

zephyr_sources_ifdef(CONFIG_AE201_GPIO gpio.c)

zephyr_sources_ifdef(CONFIG_AE201_UART uart.c)

# SPI Flash 驱动（空模板）。AE201 ICACHE 采用「现场调度 SPIF 命中」机制，
# Flash 擦写期间驱动若仍 XIP 执行会取指死锁，故整文件搬 RAM。
zephyr_sources_ifdef(CONFIG_AE201_SPI_FLASH spi_flash.c)
zephyr_code_relocate_ifdef(CONFIG_AE201_SPI_FLASH FILES spi_flash.c LOCATION RAM)

# Reuse the generic RISC-V linker script. RAM/ROM base addresses and sizes
# are derived from the /chosen/zephyr,sram and /chosen/zephyr,flash nodes.
set(SOC_LINKER_SCRIPT ${ZEPHYR_BASE}/include/zephyr/arch/riscv/common/linker.ld
    CACHE INTERNAL "SoC Linker script ${SOC_NAME}")
