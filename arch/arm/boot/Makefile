#
# arch/arm/boot/Makefile
#
# This file is subject to the terms and conditions of the GNU General Public
# License.  See the file "COPYING" in the main directory of this archive
# for more details.
#
# Copyright (C) 1995-2002 Russell King
#

MKIMAGE         := $(srctree)/scripts/mkuboot.sh

# Note: the following conditions must always be true:
#   ZRELADDR == virt_to_phys(TEXTADDR)
#   PARAMS_PHYS must be with 4MB of ZRELADDR
#   INITRD_PHYS must be in RAM

   zreladdr-$(CONFIG_ARCH_RPC)		:= 0x10008000
params_phys-$(CONFIG_ARCH_RPC)		:= 0x10000100
initrd_phys-$(CONFIG_ARCH_RPC)		:= 0x18000000
   zreladdr-$(CONFIG_ARCH_CLPS7500)	:= 0x10008000
   zreladdr-$(CONFIG_ARCH_CLPS7500)	:= 0x10008000
   zreladdr-$(CONFIG_ARCH_EBSA110)	:= 0x00008000
params_phys-$(CONFIG_ARCH_EBSA110)	:= 0x00000400
initrd_phys-$(CONFIG_ARCH_EBSA110)	:= 0x00800000
  ztextaddr-$(CONFIG_ARCH_SHARK)	:= 0x08508000
   zreladdr-$(CONFIG_ARCH_SHARK)	:= 0x08008000
   zreladdr-$(CONFIG_FOOTBRIDGE)	:= 0x00008000
params_phys-$(CONFIG_FOOTBRIDGE)	:= 0x00000100
initrd_phys-$(CONFIG_FOOTBRIDGE)	:= 0x00800000
   zreladdr-$(CONFIG_ARCH_INTEGRATOR)	:= 0x00008000
params_phys-$(CONFIG_ARCH_INTEGRATOR)	:= 0x00000100
initrd_phys-$(CONFIG_ARCH_INTEGRATOR)	:= 0x00800000
   zreladdr-$(CONFIG_ARCH_CAMELOT)	:= 0x00008000
   zreladdr-$(CONFIG_ARCH_NEXUSPCI)	:= 0x40008000
   zreladdr-$(CONFIG_ARCH_L7200)	:= 0xf0008000
# The standard locations for stuff on CLPS711x type processors
   zreladdr-$(CONFIG_ARCH_CLPS711X)	:= 0xc0028000 
params_phys-$(CONFIG_ARCH_CLPS711X)	:= 0xc0000100
# Should probably have some agreement on these...
initrd_phys-$(CONFIG_ARCH_P720T)	:= 0xc0400000
initrd_phys-$(CONFIG_ARCH_CDB89712)	:= 0x00700000
   zreladdr-$(CONFIG_ARCH_SA1100)	:= 0xc0008000
ifeq ($(CONFIG_ARCH_SA1100),y)
   zreladdr-$(CONFIG_SA1111)		:= 0xc0208000
endif
params_phys-$(CONFIG_ARCH_SA1100)	:= 0xc0000100
initrd_phys-$(CONFIG_ARCH_SA1100)	:= 0xc0800000
   zreladdr-$(CONFIG_ARCH_PXA)		:= 0xa0008000
   zreladdr-$(CONFIG_ARCH_IOP3XX)	:= 0xa0008000
params_phys-$(CONFIG_ARCH_IOP3XX)	:= 0xa0000100
   zreladdr-$(CONFIG_ARCH_IXP4XX)	:= 0x00008000
params-phys-$(CONFIG_ARCH_IXP4XX)	:= 0x00000100
   zreladdr-$(CONFIG_ARCH_OMAP)		:= 0x10008000
params_phys-$(CONFIG_ARCH_OMAP)		:= 0x10000100
initrd_phys-$(CONFIG_ARCH_OMAP)		:= 0x10800000
   zreladdr-$(CONFIG_ARCH_LH7A40X)	:= 0xc0008000
params_phys-$(CONFIG_ARCH_LH7A40X)	:= 0xc0000100
initrd_phys-$(CONFIG_ARCH_LH7A40X)	:= 0xc4000000
   zreladdr-$(CONFIG_ARCH_S3C2410)	:= 0x30008000
params_phys-$(CONFIG_ARCH_S3C2410)	:= 0x30000100
   zreladdr-$(CONFIG_ARCH_VERSATILE_PB)	:= 0x00008000
params_phys-$(CONFIG_ARCH_VERSATILE_PB)	:= 0x00000100
initrd_phys-$(CONFIG_ARCH_VERSATILE_PB)	:= 0x00800000

ZRELADDR    := $(zreladdr-y)
PARAMS_PHYS := $(params_phys-y)
INITRD_PHYS := $(initrd_phys-y)

export ZRELADDR INITRD_PHYS PARAMS_PHYS

targets := Image zImage bootpImage uImage

$(obj)/Image: vmlinux FORCE
	$(call if_changed,objcopy)
	@echo '  Kernel: $@ is ready'

$(obj)/compressed/vmlinux: $(obj)/Image FORCE
	$(Q)$(MAKE) $(build)=$(obj)/compressed $@

$(obj)/zImage:	$(obj)/compressed/vmlinux FORCE
	$(call if_changed,objcopy)
	@echo '  Kernel: $@ is ready'

quiet_cmd_uimage = UIMAGE  $@
      cmd_uimage = $(CONFIG_SHELL) $(MKIMAGE) -A arm -O linux -T kernel \
		   -C none -a $(ZRELADDR) -e $(ZRELADDR) \
		   -n 'Linux-$(KERNELRELEASE)' -d $< $@

$(obj)/uImage:	$(obj)/zImage FORCE
	$(call if_changed,uimage)
	@echo '  Image $@ is ready'

$(obj)/bootp/bootp: $(obj)/zImage initrd FORCE
	$(Q)$(MAKE) $(build)=$(obj)/bootp $@
	@:

$(obj)/bootpImage: $(obj)/bootp/bootp FORCE
	$(call if_changed,objcopy)
	@echo '  Kernel: $@ is ready'

.PHONY: initrd FORCE
initrd:
	@test "$(INITRD_PHYS)" != "" || \
	(echo This machine does not support INITRD; exit -1)
	@test "$(INITRD)" != "" || \
	(echo You must specify INITRD; exit -1)

install: $(obj)/Image
	$(CONFIG_SHELL) $(srctree)/$(src)/install.sh $(KERNELRELEASE) \
	$(obj)/Image System.map "$(INSTALL_PATH)"

zinstall: $(obj)/zImage
	$(CONFIG_SHELL) $(srctree)/$(src)/install.sh $(KERNELRELEASE) \
	$(obj)/zImage System.map "$(INSTALL_PATH)"

subdir-	    := bootp compressed
