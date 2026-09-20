# SPDX-License-Identifier: Apache-2.0

# N100 内核公共层：所有 AE 系列共享的启动/中断代码。
add_subdirectory(common)

# 具体 SoC 系列（ae101 / ae102 / ae201 / ae203 …）。
add_subdirectory(${SOC_SERIES})
