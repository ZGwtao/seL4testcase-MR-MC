# seL4 [M]emory [R]equests [M]onte [C]arlo simulation

### Getting the project
```shell
$ mkdir seL4testcase-mrmc
$ cd seL4testcase-mrmc
$ repo init -u https://github.com/ZGwtao/seL4testcase-MR-MC.git
$ repo sync
```
### Build
```shell
$ mkdir build
$ cd build
$ ../init-build.sh \
      -DPLATFORM=<platform-name> \
      -DRELEASE=[TRUE|FALSE] -DSIMULATION=[TRUE|FALSE] \
      -DLibVKAAllowPoolOperations=[ON|OFF] -DLibAllocmanAllowPoolOperations=[ON|OFF] \  # options for CapBuddy (ON)
      -DKernelRetypeFanOutLimit=1024 -DLibSel4MuslcSysMorecoreBytes=0 \  # enable when CapBuddy options are enabled
      -DKernelBenchmarks=[track_utilisation|none]
$ ninja
```
### Example
```shell
$ ../init-build.sh -DPLATFORM=bcm2837 -DKernelSel4Arch=aarch64 \
      -DSIMULATION=ON -DKernelDebugBuild=ON \
      -DLibVKAAllowPoolOperations=ON -DLibAllocmanAllowPoolOperations=ON \
      -DKernelRetypeFanOutLimit=1024 -DLibSel4MuslcSysMorecoreBytes=0 \
      -DKernelBenchmarks=track_utilisation -DMRMCTestInfoEnable=OFF
  # loading initial cache file /home/rafiki/seL4/MRMC/projects/seL4testcase-MR-MC/settings.cmake
  # -- Found GCC with prefix aarch64-linux-gnu-
  # -- libmuslc architecture: 'aarch64' (from KernelSel4Arch 'aarch64')
  # -- Detecting cached version of: musllibc
  # --   Found valid cache entry for musllibc
  # -- Configuring done
  # -- Generating done
  # -- Build files have been written to: /home/rafiki/seL4/MRMC/build
$ ninja
  # [0/1] Re-running CMake...
  # -- Found GCC with prefix aarch64-linux-gnu-
  # -- libmuslc architecture: 'aarch64' (from KernelSel4Arch 'aarch64')
  # -- Detecting cached version of: musllibc
  # --   Found valid cache entry for musllibc
  # -- Configuring done
  # -- Generating done
  # -- Build files have been written to: /home/rafiki/seL4/MRMC/build
  # [9/9] Generating ../images/sel4testcase-mce-image-arm-bcm2837
$ ./simulate
  # ./simulate: QEMU command: qemu-system-aarch64 -machine raspi3  -nographic -serial null -serial mon:stdio -m size=1024M  -kernel images/sel4testcase-mce-image-arm-bcm2837 
  # ELF-loader started on CPU: ARM Ltd. Cortex-A53 r0p4
  #   paddr=[16dc000..18bd0ff]
  # No DTB passed in from boot loader.
  # Looking for DTB in CPIO archive...found at 17d4ae8.
  # Loaded DTB from 17d4ae8.
  #    paddr=[123d000..1240fff]
  # ELF-loading image 'kernel' to 1000000
  #   paddr=[1000000..123cfff]
  #   vaddr=[ffffff8001000000..ffffff800123cfff]
  #   virt_entry=ffffff8001000000
  # ELF-loading image 'sel4testcase-mce' to 1241000
  #   paddr=[1241000..12e6fff]
  #   vaddr=[400000..4a5fff]
  #   virt_entry=4023e8
  # Enabling MMU and paging
  # Jumping to kernel-image entry point...
  # 
  # Warning:  gpt_cntfrq 62500000, expected 19200000
  # Bootstrapping kernel
  # available phys memory regions: 1
  #   [1000000..8000000]
  # reserved virt address space regions: 3
  #   [ffffff8001000000..ffffff800123d000]
  #   [ffffff800123d000..ffffff8001240593]
  #   [ffffff8001241000..ffffff80012e7000]
  # Booting all finished, dropped to user space
  # 
  # Warning: using printf before serial is set up. This only works as your
  # printf is backed by seL4_Debug_PutChar()
  # 
  # >>>>>>>> __func_entry__ <<<<<<<
  # 
  # *********** Benchmark ***********
  # 
  # [DONE]: total 14985918 iteration 80000
  # 
  # *********** Benchmark ***********
  # 
  # CPU cycles spent: 13950883878
  # 
  # >>>>>>>> __func__exit__ <<<<<<<
```
