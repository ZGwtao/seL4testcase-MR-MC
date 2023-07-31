# Copyright 2023, zhuguangtao@iie.ac.cn, SKLOIS
# --------------------------------------------------
# Borrowd from https://github.com/seL4/sel4test.git
# --------------------------------------------------
# Copyright 2019, Data61, CSIRO (ABN 41 687 119 230)
#
# SPDX-License-Identifier: BSD-2-Clause

set(SIMULATION OFF CACHE BOOL "Include only simulation compatible tests")
set(RELEASE OFF CACHE BOOL "Performance optimized build")
set(VERIFICATION OFF CACHE BOOL "Only verification friendly kernel features")
set(BAMBOO OFF CACHE BOOL "Enable machine parseable output")
set(DOMAINS OFF CACHE BOOL "Test multiple domains")
set(SMP OFF CACHE BOOL "(if supported) Test SMP kernel")
set(NUM_NODES "" CACHE STRING "(if SMP) the number of nodes (default 4)")
set(ARM_HYP OFF CACHE BOOL "Hyp mode for ARM platforms")
set(MCS OFF CACHE BOOL "MCS kernel")
set(KernelSel4Arch "" CACHE STRING "aarch32, aarch64, arm_hyp, ia32, x86_64, riscv32, riscv64")
set(LibSel4TestPrinterRegex ".*" CACHE STRING "A POSIX regex pattern used to filter tests")
set(LibSel4TestPrinterHaltOnTestFailure OFF CACHE BOOL "Halt on the first test failure")
mark_as_advanced(CLEAR LibSel4TestPrinterRegex LibSel4TestPrinterHaltOnTestFailure)