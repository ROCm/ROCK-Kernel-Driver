# This file is included by the global makefile so that you can add your own
# architecture-specific flags and dependencies. Remember to do have actions
# for "archclean" and "archdep" for cleaning up and making dependencies for
# this architecture
#
# This file is subject to the terms and conditions of the GNU General Public
# License.  See the file "COPYING" in the main directory of this archive
# for more details.
#
# Copyright (C) 1994 by Linus Torvalds
# Changes for PPC by Gary Thomas
# Rewritten by Cort Dougan and Paul Mackerras
# Adjusted for PPC64 by Tom Gall
#

KERNELLOAD	=0xc000000000000000

LDFLAGS		:= -m elf64ppc
LDFLAGS_vmlinux	= -Bstatic \
		-e $(KERNELLOAD) -Ttext $(KERNELLOAD)
CFLAGS		:= $(CFLAGS) -msoft-float -pipe \
		-Wno-uninitialized -mminimal-toc -mtraceback=full \
		-finline-limit-2000 -mcpu=power4
CPP		= $(CC) -E $(CFLAGS)


HEAD := arch/ppc64/kernel/head.o

core-y			+= arch/ppc64/kernel/ arch/ppc64/mm/ arch/ppc64/lib/
core-$(CONFIG_XMON)	+= arch/ppc64/xmon/

MAKEBOOT = $(MAKE) -C arch/$(ARCH)/boot

ifdef CONFIG_PPC_PSERIES
BOOT_TARGETS = zImage znetboot.initrd zImage.initrd
endif

ifdef CONFIG_PPC_ISERIES
BOOT_TARGETS = vmlinux.sminitrd vmlinux.initrd vmlinux.sm
endif

$(BOOT_TARGETS): vmlinux
	@$(MAKEBOOT) $@

znetboot: vmlinux
ifdef CONFIG_SMP
	cp -f vmlinux /tftpboot/vmlinux.smp
else
	cp -f vmlinux /tftpboot/vmlinux
endif
	@$(MAKEBOOT) $@

%_config: arch/ppc64/configs/%_defconfig
	rm -f .config arch/ppc64/defconfig
	cp -f arch/ppc64/configs/$(@:config=defconfig) arch/ppc64/defconfig

archclean:
	@$(MAKEBOOT) clean

archmrproper:


prepare: include/asm-$(ARCH)/offsets.h

arch/$(ARCH)/kernel/asm-offsets.s: include/asm include/linux/version.h \
				   include/config/MARKER

include/asm-$(ARCH)/offsets.h.tmp: arch/$(ARCH)/kernel/asm-offsets.s
	@$(generate-asm-offsets.h) < $< > $@

include/asm-$(ARCH)/offsets.h: include/asm-$(ARCH)/offsets.h.tmp
	@echo -n '  Generating $@'
	@$(update-if-changed)

CLEAN_FILES += include/asm-$(ARCH)/offsets.h.tmp \
	       include/asm-$(ARCH)/offsets.h
