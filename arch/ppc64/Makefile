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

KERNELLOAD	:= 0xc000000000000000

LDFLAGS		:= -m elf64ppc
LDFLAGS_vmlinux	:= -Bstatic -e $(KERNELLOAD) -Ttext $(KERNELLOAD)
LDFLAGS_BLOB	:= --format binary --oformat elf64-powerpc
CFLAGS		+= -msoft-float -pipe -Wno-uninitialized -mminimal-toc \
		-mtraceback=full -mcpu=power4

head-y := arch/ppc64/kernel/head.o

libs-y				+= arch/ppc64/lib/
core-y				+= arch/ppc64/kernel/
core-y				+= arch/ppc64/mm/
core-$(CONFIG_XMON)		+= arch/ppc64/xmon/
drivers-$(CONFIG_OPROFILE)	+= arch/ppc64/oprofile/

boot := arch/ppc64/boot

boottarget-$(CONFIG_PPC_PSERIES) := zImage zImage.initrd
boottarget-$(CONFIG_PPC_ISERIES) := vmlinux.sminitrd vmlinux.initrd vmlinux.sm
$(boottarget-y): vmlinux
	$(Q)$(MAKE) $(build)=$(boot) $(boot)/$@

%_config: arch/ppc64/configs/%_defconfig
	rm -f .config arch/ppc64/defconfig
	cp -f arch/ppc64/configs/$(@:config=defconfig) arch/ppc64/defconfig

archclean:
	$(Q)$(MAKE) $(clean)=$(boot)

prepare: include/asm-ppc64/offsets.h

arch/ppc64/kernel/asm-offsets.s: include/asm include/linux/version.h \
				   include/config/MARKER

include/asm-ppc64/offsets.h: arch/ppc64/kernel/asm-offsets.s
	$(call filechk,gen-asm-offsets)

CLEAN_FILES += include/asm-ppc64/offsets.h
