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
   zreladdr-$(CONFIG_ARCH_ANAKIN)	:= 0x20008000
   zreladdr-$(CONFIG_ARCH_IOP3XX)	:= 0xa0008000
params_phys-$(CONFIG_ARCH_IOP3XX)	:= 0xa0000100
   zreladdr-$(CONFIG_ARCH_ADIFCC)	:= 0xc0008000
params_phys-$(CONFIG_ARCH_ADIFCC)	:= 0xc0000100

ZRELADDR    := $(zreladdr-y)
ZTEXTADDR   := $(ztextaddr-y)
PARAMS_PHYS := $(params_phys-y)
INITRD_PHYS := $(initrd_phys-y)
#
# We now have a PIC decompressor implementation.  Decompressors running
# from RAM should not define ZTEXTADDR.  Decompressors running directly
# from ROM or Flash must define ZTEXTADDR (preferably via the config)
# FIXME: Previous assignment to ztextaddr-y is lost here. See SHARK
ifeq ($(CONFIG_ZBOOT_ROM),y)
ZTEXTADDR	:= $(CONFIG_ZBOOT_ROM_TEXT)
ZBSSADDR	:= $(CONFIG_ZBOOT_ROM_BSS)
else
ZTEXTADDR	:= 0
ZBSSADDR	:= ALIGN(4)
endif

export	ZTEXTADDR ZBSSADDR ZRELADDR INITRD_PHYS PARAMS_PHYS

targets := Image zImage bootpImage

$(obj)/Image: vmlinux FORCE
	$(call if_changed,objcopy)
	@echo '  Kernel: $@ is ready'

$(obj)/zImage:	$(obj)/compressed/vmlinux FORCE
	$(call if_changed,objcopy)
	@echo '  Kernel: $@ is ready'

quite_cmd_uimage = UIMAGE $@
      cmd_uimage = $(CONFIG_SHELL) $(MKIMAGE) -A arm -O linux -T kernel \
		   -C none -a $(ZRELADDR) -e $(ZRELADDR) \
		   -n 'Linux-$(KERNELRELEASE)' -d $< $@

targets += uImage
$(obj)/uImage:	$(obj)/zImage
	$(call if_changed,uimage)
	@echo '  Image $@ is ready'

$(obj)/bootpImage: $(obj)/bootp/bootp FORCE
	$(call if_changed,objcopy)
	@echo '  Kernel: $@ is ready'

$(obj)/compressed/vmlinux: vmlinux FORCE
	$(Q)$(MAKE) $(build)=$(obj)/compressed $@

$(obj)/bootp/bootp: $(obj)/zImage initrd FORCE
	$(Q)$(MAKE) $(build)=$(obj)/bootp $@

.PHONY: initrd
initrd:
	@test "$(INITRD_PHYS)" != "" || \
	(echo This machine does not support INITRD; exit -1)
	@test "$(INITRD)" != "" || \
	(echo You must specify INITRD; exit -1)

install: $(obj)/Image
	$(CONFIG_SHELL) $(obj)/install.sh \
	$(VERSION).$(PATCHLEVEL).$(SUBLEVEL)$(EXTRAVERSION) \
	$(obj)/Image System.map "$(INSTALL_PATH)"

zinstall: $(obj)/zImage
	$(CONFIG_SHELL) $(obj)/install.sh \
	$(VERSION).$(PATCHLEVEL).$(SUBLEVEL)$(EXTRAVERSION) \
	$(obj)/zImage System.map "$(INSTALL_PATH)"

subdir-	    := bootp compressed
