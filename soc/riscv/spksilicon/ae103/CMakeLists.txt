# SPDX-License-Identifier: Apache-2.0

zephyr_sources(soc.c)

# 可控 secondary-core / 多镜像启动流程（AE103_BOOT_MODE 宏控）。
zephyr_sources(ae103_boot.c)

zephyr_sources_ifdef(CONFIG_AE103_TIMER timer.c)

# Reuse the generic RISC-V linker script. RAM/ROM base addresses and sizes
# are derived from the /chosen/zephyr,sram and /chosen/zephyr,flash nodes.
set(SOC_LINKER_SCRIPT ${ZEPHYR_BASE}/include/zephyr/arch/riscv/common/linker.ld
    CACHE INTERNAL "SoC Linker script ${SOC_NAME}")
