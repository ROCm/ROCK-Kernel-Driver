#
# arch/arm/boot/Makefile
#
# This file is subject to the terms and conditions of the GNU General Public
# License.  See the file "COPYING" in the main directory of this archive
# for more details.
#
# Copyright (C) 1995-2000 Russell King
#

SYSTEM	=$(TOPDIR)/vmlinux

ifeq ($(CONFIG_CPU_26),y)
ZTEXTADDR	 = 0x02080000
PARAMS_PHYS	 = 0x0207c000
INITRD_PHYS	 = 0x02180000
INITRD_VIRT	 = 0x02180000
endif

ifeq ($(CONFIG_ARCH_RPC),y)
ZTEXTADDR	 = 0x10008000
PARAMS_PHYS	 = 0x10000100
INITRD_PHYS	 = 0x18000000
INITRD_VIRT	 = 0xc8000000
endif

ifeq ($(CONFIG_ARCH_CLPS7500),y)
ZTEXTADDR	 = 0x10008000
endif

ifeq ($(CONFIG_ARCH_EBSA110),y)
ZTEXTADDR	 = 0x00008000
PARAMS_PHYS	 = 0x00000400
INITRD_PHYS	 = 0x00800000
INITRD_VIRT	 = 0xc0800000
endif

ifeq ($(CONFIG_ARCH_SHARK),y)
ZTEXTADDR	 = 0x08508000
ZRELADDR	 = 0x08008000
endif

ifeq ($(CONFIG_FOOTBRIDGE),y)
ZTEXTADDR	 = 0x00008000
PARAMS_PHYS	 = 0x00000100
INITRD_PHYS	 = 0x00800000
INITRD_VIRT	 = 0xc0800000
endif

ifeq ($(CONFIG_ARCH_INTEGRATOR),y)
ZTEXTADDR	 = 0x00008000
PARAMS_PHYS	 = 0x00000100
INITRD_PHYS	 = 0x00800000
INITRD_VIRT	 = 0xc0800000
endif

ifeq ($(CONFIG_ARCH_NEXUSPCI),y)
ZTEXTADDR	 = 0x40200000
ZRELADDR	 = 0x40008000
endif

ifeq ($(CONFIG_ARCH_L7200),y)
# RAM based kernel
#ZTEXTADDR	 = 0xf0400000
#ZRELADDR	 = 0xf0008000

# FLASH based kernel
ZTEXTADDR	 = 0x00010000
ZRELADDR	 = 0xf0008000
ZBSSADDR	 = 0xf03e0000
endif

ifeq ($(CONFIG_ARCH_P720T),y)
ZTEXTADDR	 = 0xc0018000
ZRELADDR	 = 0xc0018000
PARAMS_PHYS	 = 0xc0000100
INITRD_PHYS	 = 0xc0400000
INITRD_VIRT	 = 0xc0400000
endif

ifeq ($(CONFIG_ARCH_SA1100),y)
ZTEXTADDR	 = 0xc0008000
ZRELADDR	 = 0xc0008000
ifeq ($(CONFIG_SA1100_VICTOR),y)
  ZTEXTADDR	 = 0x00002000
  ZBSSADDR	 = 0xc0100000
endif
ifeq ($(CONFIG_SA1100_SHERMAN),y)
  ZTEXTADDR      = 0x00050000   
  ZBSSADDR       = 0xc0100000
endif
ifeq ($(CONFIG_SA1100_GRAPHICSCLIENT),y)
  ZTEXTADDR	 = 0xC0200000
endif
endif

#
# If you don't define ZRELADDR above,
# then it defaults to ZTEXTADDR
#
ifeq ($(ZRELADDR),)
ZRELADDR	= $(ZTEXTADDR)
endif

export	SYSTEM ZTEXTADDR ZBSSADDR ZRELADDR INITRD_PHYS INITRD_VIRT PARAMS_PHYS

Image:	$(CONFIGURE) $(SYSTEM)
	$(OBJCOPY) $(SYSTEM) $@

bzImage: zImage

zImage:	$(CONFIGURE) compressed/vmlinux
	$(OBJCOPY) compressed/vmlinux $@

bootpImage: bootp/bootp
	$(OBJCOPY) bootp/bootp $@

compressed/vmlinux: $(TOPDIR)/vmlinux dep
	@$(MAKE) -C compressed vmlinux

bootp/bootp: zImage initrd
	@$(MAKE) -C bootp bootp

initrd:
	@test "$(INITRD_VIRT)" != "" || (echo This architecture does not support INITRD; exit -1)
	@test "$(INITRD)" != "" || (echo You must specify INITRD; exit -1)

install: $(CONFIGURE) Image
	sh ./install.sh $(VERSION).$(PATCHLEVEL).$(SUBLEVEL)$(EXTRAVERSION) Image $(TOPDIR)/System.map "$(INSTALL_PATH)"

zinstall: $(CONFIGURE) zImage
	sh ./install.sh $(VERSION).$(PATCHLEVEL).$(SUBLEVEL)$(EXTRAVERSION) zImage $(TOPDIR)/System.map "$(INSTALL_PATH)"

clean:
	$(RM) Image zImage bootpImage
	@$(MAKE) -C compressed clean
	@$(MAKE) -C bootp clean

dep:
