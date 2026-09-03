# SPDX-License-Identifier: Apache-2.0
#
# spksilicon N100 内核公共层：所有基于 Nuclei N100 内核的 AE 系列
# （AE101/AE102/AE103/AE201/AE203…）共享的启动/中断代码。
#
# N100 内核 mtvec 只读，不能沿用通用 riscv-privileged/vector.S；
# 自定义 vector.S（CONFIG_NUCLEI_N100_SPECIAL 宏控 mtvec）+ 本地化
# soc_irq.S / soc_common_irq.c（补回移除 RISCV_PRIVILEGED 后缺失的符号）。

zephyr_sources(vector.S)
zephyr_sources(soc_irq.S)
zephyr_sources(soc_common_irq.c)
