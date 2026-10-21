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
# 引用方（soc.c / clock.c / drivers/*_drv.c）现通过 ae201_sysctl_sl.h 使用；
# 父仓库的 WDT 模块经 clock.h 间接依赖（见下方 clock.c 说明）。
#
# 2026-10-19：GPIO / UART / TIMER / SPIF 四个外设模块与 systimer 也已迁出本
# 目录，落在 ecfw-library/…/ae201/{sl,drivers}/，编译登记同样在应用侧
# configs/spksilicon/spk32ae/ae201/CMakeLists.txt。本目录只剩 SoC glue：
#   soc.c      中断初始化 / 复位
#   clock.c    主频事实源 + 变更通知（被父仓库驱动反向依赖）
#   ae201_boot.c 多镜像启动
# 故这里不再有任何 zephyr_sources_ifdef(...) 的外设模块登记。

# 时钟域管理：主频单一事实源 + 频率拉取 + 变更通知。WDT（ae201_wdt_sl.c）与
# drivers/ae201_uart_drv.c 依赖
# ae201_clock_freq_get（早期路径纯 MMIO），与已迁出的 SYSCTL 同属「无条件
# 编译」之列，故这里不设 Kconfig 开关。
zephyr_sources(clock.c)

# 可控 secondary-core / 多镜像启动流程（AE201_BOOT_MODE 宏控）。
zephyr_sources(ae201_boot.c)

# 看门狗（WDT）驱动已迁出本目录，改由父仓库
# ecfw-library/spksilicon/spk32ae/ae201/sl/ 提供（2026-10-19 由 wdt/ 下的
# drv + init + reg 合并为 ae201_wdt_sl.{c,h} / ae201_wdt_reg.h），编译登记在
# configs/spksilicon/spk32ae/ae201/CMakeLists.txt。本目录不再持有其源文件。
#
# 原 wdt.c 整文件被 zephyr_sources_ifdef(CONFIG_WDOG_INIT …) 绑死，该开关一关
# 通用驱动能力全部消失。合并后的做法是**函数级**条件编译：
#   · 寄存器级 API + 标准驱动 —— 始终编译；
#   · _WdogInit（reset.S → z_prep_c 之前）—— 仅 CONFIG_WDOG_INIT=y 时生成。
# 引用方通过 ae201_wdt_sl.h 使用（唯一外部约定仍是符号 _WdogInit）。
#
# 注：CONFIG_WDOG_INIT 仍定义在本目录 Kconfig.soc —— reset.S 的 #ifdef 用它；
# 该符号已为 promptless，由应用层 AE201_WDT_INIT 派生。

# 外设模块（GPIO / UART / TIMER / SPIF）与 systimer 的源文件登记已移至
# configs/spksilicon/spk32ae/ae201/CMakeLists.txt —— 见文件头「维护边界」。
# 其中 SPIF 必须整文件搬 RAM（AE201 ICACHE 现场调度 SPIF 命中），其
# zephyr_code_relocate(FILES <绝对路径> LOCATION RAM) 也在那里。

# Reuse the generic RISC-V linker script. RAM/ROM base addresses and sizes
# are derived from the /chosen/zephyr,sram and /chosen/zephyr,flash nodes.
set(SOC_LINKER_SCRIPT ${ZEPHYR_BASE}/include/zephyr/arch/riscv/common/linker.ld
    CACHE INTERNAL "SoC Linker script ${SOC_NAME}")
