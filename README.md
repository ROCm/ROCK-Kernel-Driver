### AMD Radeon OpenCompute Kernel driver

* Please see packages/ folder for binary install packages

### Installation and Configuration guide

#### What's New in this tree ?

* dGPU support for Fiji
* device and host memory support
* multiple GPU support
* host memory allocations are shared between GPUs

#### Known issues in this tree

* Segfault on GPU access to unmapped memory not implemented on dGPU

#### Package Contents

The packages/ folder contains packages for Ubuntu 14.04 and Fedora 22

The kernel image is built from a source tree based on the 4.1 upstream
release plus :

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

* Downloading the kernel binaries from the repo  
`git clone https://github.com/RadeonOpenCompute/ROCK-Kernel-Driver.git`

* Go to the top of the repo:  
`cd ROCK-Kernel-Driver`

* Configure udev to allow any user to access /dev/kfd. As root, use a text
editor to create /etc/udev/rules.d/kfd.rules containing one line:
KERNEL=="kfd", MODE="0666", Or you could use the following command:  
`echo  "KERNEL==\"kfd\", MODE=\"0666\"" | sudo tee /etc/udev/rules.d/kfd.rules`

* For Ubuntu, install the kernel and libhsakmt packages using:  
`sudo dpkg -i packages/ubuntu/*.deb`

* For Fedora, install the kernel and libhsakmt packages using:
`sudo rpm -i packages/fedora/*.rpm`

* Reboot the system to install the new kernel and enable the HSA kernel driver:  
`sudo reboot`


##### Obtaining kernel and libhsakmt source code

* Source code used to build the kernel is in this repo. Source code to
  build libhsakmt is in the ROCT-Thunk repository 

###LICENSE

The following lists the different licenses that apply to the different
components in this repository:

* the Linux kernel images are covered by the modified GPL license in COPYING
* the firmware image is covered by the license in LICENSE.ucode
