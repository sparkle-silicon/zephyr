# SPDX-License-Identifier: Apache-2.0

set(riscv_mabi "lp")
set(riscv_march "rv")

if(CONFIG_64BIT)
    string(CONCAT riscv_mabi  ${riscv_mabi} "64")
    string(CONCAT riscv_march ${riscv_march} "64")
    list(APPEND TOOLCHAIN_C_FLAGS -mcmodel=medany)
    list(APPEND TOOLCHAIN_LD_FLAGS -mcmodel=medany)
else()
    string(CONCAT riscv_mabi  "i" ${riscv_mabi} "32")
    string(CONCAT riscv_march ${riscv_march} "32")
endif()

if (CONFIG_RISCV_ISA_RV32E)
    string(CONCAT riscv_mabi ${riscv_mabi} "e")
    string(CONCAT riscv_march ${riscv_march} "e")
else()
    string(CONCAT riscv_march ${riscv_march} "i")
endif()

if (CONFIG_RISCV_ISA_EXT_M)
    string(CONCAT riscv_march ${riscv_march} "m")
endif()
if (CONFIG_RISCV_ISA_EXT_A)
    string(CONCAT riscv_march ${riscv_march} "a")
endif()

if(CONFIG_FPU)
    if(CONFIG_CPU_HAS_FPU_DOUBLE_PRECISION)
        if(CONFIG_FLOAT_HARD)
            string(CONCAT riscv_mabi ${riscv_mabi} "d")
        endif()
        string(CONCAT riscv_march ${riscv_march} "fd")
    else()
        if(CONFIG_FLOAT_HARD)
            string(CONCAT riscv_mabi ${riscv_mabi} "f")
        endif()
        string(CONCAT riscv_march ${riscv_march} "f")
    endif()
endif()

if(CONFIG_RISCV_ISA_EXT_C)
    string(CONCAT riscv_march ${riscv_march} "c")
endif()

# Z* 扩展（zicsr/zifencei/zba/zbb/zbc/zbs）在 RISC-V ISA 20191213 规范中才被拆分为
# 独立扩展。GCC < 12 隐含这些扩展，且不认识 "-march=..._zxxx" 的显式写法
# （报 "unsupported ISA subset 'z'"）。因此仅在 GCC >= 12 时显式拼写；
# 旧工具链（如 Nuclei GCC 9.2.0）靠隐含支持，语义等价。

# Zephyr 的 cross-compile 工具链（ZEPHYR_TOOLCHAIN_VARIANT=cross-compile）跳过 CMake
# 的编译器自动检测，导致 CMAKE_C_COMPILER_VERSION 为空。这里主动用 -dumpversion
# 探测真实 GCC 版本，否则上面的 VERSION_GREATER_EQUAL 判断恒为 false，
# 新工具链（GCC 12.2.0）也拼不出 _zicsr，csr 指令会报 unrecognized opcode。
if(NOT CMAKE_C_COMPILER_VERSION)
  execute_process(
    COMMAND ${CMAKE_C_COMPILER} -dumpversion
    OUTPUT_VARIABLE CMAKE_C_COMPILER_VERSION
    OUTPUT_STRIP_TRAILING_WHITESPACE
  )
endif()

if (CMAKE_C_COMPILER_VERSION VERSION_GREATER_EQUAL 12.0.0)
  if(CONFIG_RISCV_ISA_EXT_ZICSR)
      string(CONCAT riscv_march ${riscv_march} "_zicsr")
  endif()

  if(CONFIG_RISCV_ISA_EXT_ZIFENCEI)
      string(CONCAT riscv_march ${riscv_march} "_zifencei")
  endif()

  if(CONFIG_RISCV_ISA_EXT_ZBA)
      string(CONCAT riscv_march ${riscv_march} "_zba")
  endif()

  if(CONFIG_RISCV_ISA_EXT_ZBB)
      string(CONCAT riscv_march ${riscv_march} "_zbb")
  endif()

  if(CONFIG_RISCV_ISA_EXT_ZBC)
      string(CONCAT riscv_march ${riscv_march} "_zbc")
  endif()

  if(CONFIG_RISCV_ISA_EXT_ZBS)
      string(CONCAT riscv_march ${riscv_march} "_zbs")
  endif()
endif()

list(APPEND TOOLCHAIN_C_FLAGS -mabi=${riscv_mabi} -march=${riscv_march})
list(APPEND TOOLCHAIN_LD_FLAGS NO_SPLIT -mabi=${riscv_mabi} -march=${riscv_march})
