### AMD Radeon Open Compute Kernel driver

#### What's New in this tree ?

* dGPU support for Fiji
* device and host memory support
* multiple GPU support
* host memory allocations are shared between GPUs

#### Known Issues

* On consumer grade products (Nano, Fury, Fury X), thermal control is not
  working correctly. As a workaround, fans are hardcoded to 100% to prevent
  overheating.

#### Package Contents

The kernel image is built from a source tree based on the 4.9 upstream
release plus:

* Features in the HSA kernel driver ("amdkfd") that are not yet
  upstreamed to the mainline Linux kernel.
* Changes in the AMDGPU kernel driver ("amdgpu") that may not yet be
  upstreamed to the mainline Linux kernel.

##### Note regarding libhsakmt compatibility
Please note that the libhsakmt library in this repository is NOT compatible
with amdkfd that is distributed as part of the mainline Linux kernel
from 3.19 and onward.

#### Target Platform

This release is intended for use with any hardware configuration that
contains only a Kaveri or Carrizo APU, or configurations which contain
an Intel Haswell or newer CPU plus Fiji dGPUs.

APU motherboards must support run latest BIOS version and have the IOMMU
enabled in the BIOS.

The following is a reference hardware configuration that was used for
testing purposes:

APU Config:
* APU:            AMD A10-7850K APU
* Motherboard:    ASUS A88X-PRO motherboard (ATX form factor)
* Memory:         G.SKILL Ripjaws X Series 16GB (2 x 8GB) 240-Pin DDR3 SDRAM DDR3 2133
* OS:             Ubuntu 14.04 64-bit edition
* No discrete GPU present in the system

dGPU Config:
* CPU:            Intel i7-4790
* Motherboard:    ASUS Z97-PRO
* Memory:         G.Skill Ripjaws 4 32GB RAM (4 x 8GB)
* OS:             Ubuntu 14.04.03 64-bit edition
* dGPU:           ASUS R9 Nano

#### Installing and configuring the kernel

> **NOTE:** Binary packages are no longer part of this git repository. Please
> refer to the [ROCm project](https://github.com/RadeonOpenCompute/ROCm/wiki)
> for instructions on configuring the AMD apt/yum package server

#### Config files for building the kernel

The configuration used to build our kernel can be re-created by running:
`make rock-rel_defconfig`

This config is based on the Ubuntu 14.04 build patches by Canonical.

##### Obtaining kernel and libhsakmt source code

* Source code used to build the kernel is in this repo. Source code to
  build libhsakmt is in the
  [ROCT-Thunk-Interface](https://github.com/RadeonOpenCompute/ROCT-Thunk-Interface)
  repository

###LICENSE

The following lists the different licenses that apply to the different
components in this repository:

* the Linux kernel images are covered by the modified GPL license in COPYING
* the firmware image is covered by the license in LICENSE.ucode
