# Copyright 2023, zhuguangtao@iie.ac.cn, SKLOIS
# --------------------------------------------------
# Borrowd from https://github.com/seL4/sel4test.git
# --------------------------------------------------
# Copyright 2019, Data61, CSIRO (ABN 41 687 119 230)
#
# SPDX-License-Identifier: BSD-2-Clause
#

cmake_minimum_required(VERSION 3.7.2)

include(settings.cmake)

project(sel4testcase C CXX ASM)

find_package(seL4 REQUIRED)
find_package(elfloader-tool REQUIRED)

## (Deprecated) Set the roottask CNode size to be large enough for DTB, timer caps, etc
## Note that this works for the platforms that we have at the moment, and may need to be
## increased in the future (equals 13 in https://github.com/seL4/sel4test.git)
#
# Now (the future we are in), 13 is not enough for CapBuddy support that Monte Carlo Exp
# needs, (single-level cspace with root_cnode_size 2^13 is too small), we now try >=15
#
set(KernelRootCNodeSizeBits 16 CACHE INTERNAL "")

sel4_import_kernel()

if((NOT Sel4testAllowSettingsOverride) AND (KernelArchARM OR KernelArchRiscV))
    # Elfloader settings that correspond to how Data61 sets its boards up.
    ApplyData61ElfLoaderSettings(${KernelPlatform} ${KernelSel4Arch})
endif()
elfloader_import_project()

# mce => Monte Carlo Experiment (for Memory Requests)
add_subdirectory(test)

if(SIMULATION)
    include(simulation)
    if(KernelSel4ArchX86_64)
        SetSimulationScriptProperty(MEM_SIZE "3G")
    endif()
    if(KernelPlatformQEMUArmVirt)
        SetSimulationScriptProperty(MEM_SIZE "2G")
    endif()
    GenerateSimulateScript()
endif()