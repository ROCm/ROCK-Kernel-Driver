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
#

# This must match PAGE_OFFSET in include/asm-ppc/page.h.
KERNELLOAD	=$(CONFIG_KERNEL_START)

LDFLAGS_BLOB	:= --format binary --oformat elf32-powerpc
LDFLAGS_vmlinux	= -Ttext $(KERNELLOAD) -Bstatic
CPPFLAGS	:= $(CPPFLAGS) -I$(TOPDIR)/arch/$(ARCH)
AFLAGS		:= $(AFLAGS) -I$(TOPDIR)/arch/$(ARCH)
CFLAGS		:= $(CFLAGS) -I$(TOPDIR)/arch/$(ARCH) -msoft-float -pipe \
		-ffixed-r2 -Wno-uninitialized -mmultiple -mstring
CPP		= $(CC) -E $(CFLAGS)

ifdef CONFIG_4xx
CFLAGS := $(CFLAGS) -Wa,-m405
endif

ifdef CONFIG_PPC64BRIDGE
CFLAGS := $(CFLAGS) -Wa,-mppc64bridge
endif

ifdef CONFIG_MORE_COMPILE_OPTIONS
# Use sed to remove the quotes.
  CFLAGS += $(shell echo $(CONFIG_COMPILE_OPTIONS) | sed -e 's/"//g')
endif

head-y				:= head.o
head-$(CONFIG_8xx)		:= head_8xx.o
head-$(CONFIG_4xx)		:= head_4xx.o
head-$(CONFIG_440)		:= head_44x.o

HEAD := arch/ppc/kernel/$(head-y)
ifdef CONFIG_6xx
  HEAD += arch/ppc/kernel/idle_6xx.o
endif

core-y				+= arch/ppc/kernel/ arch/ppc/platforms/ \
				   arch/ppc/mm/ arch/ppc/lib/ arch/ppc/syslib/
core-$(CONFIG_4xx)		+= arch/ppc/platforms/4xx/
core-$(CONFIG_MATH_EMULATION)	+= arch/ppc/math-emu/
core-$(CONFIG_XMON)		+= arch/ppc/xmon/
core-$(CONFIG_APUS)		+= arch/ppc/amiga/
drivers-$(CONFIG_8xx)		+= arch/ppc/8xx_io/
drivers-$(CONFIG_4xx)		+= arch/ppc/4xx_io/
drivers-$(CONFIG_8260)		+= arch/ppc/8260_io/

BOOT_TARGETS = zImage zImage.initrd znetboot znetboot.initrd pImage vmlinux.sm

.PHONY: $(BOOT_TARGETS) clean archclean archmrproper

all: zImage

AFLAGS_vmlinux.lds.o	:= -Upowerpc

# All the instructions talk about "make bzImage".
bzImage: zImage

$(BOOT_TARGETS): vmlinux
	$(Q)$(MAKE) $(build)=arch/ppc/boot $@

%_config: arch/ppc/configs/%_defconfig
	rm -f .config arch/ppc/defconfig
	cp -f arch/ppc/configs/$(@:config=defconfig) .config

archclean:
	$(Q)$(MAKE) $(clean)=arch/ppc/boot

archmrproper:

prepare: include/asm-$(ARCH)/offsets.h checkbin

arch/$(ARCH)/kernel/asm-offsets.s: include/asm include/linux/version.h \
				   include/config/MARKER

include/asm-$(ARCH)/offsets.h.tmp: arch/$(ARCH)/kernel/asm-offsets.s
	@$(generate-asm-offsets.h) < $< > $@

include/asm-$(ARCH)/offsets.h: include/asm-$(ARCH)/offsets.h.tmp
	@echo -n '  Generating $@'
	@$(update-if-changed)

ifdef CONFIG_6xx
# Ensure this is binutils 2.12.1 (or 2.12.90.0.7) or later
NEW_AS	:= $(shell echo dssall | $(AS) -o /dev/null >/dev/null 2>&1 ; echo $$?)
GOODVER	:= 2.12.1
else
NEW_AS	:= 0
endif

ifneq ($(NEW_AS),0)
checkbin:
	@echo -n '*** ${VERSION}.${PATCHLEVEL} kernels no longer build'
	@echo 'correctly with old versions of binutils.'
	@echo '*** Please upgrade your binutils to ${GOODVER} or newer'
	@false
else
checkbin:
	@true
endif

CLEAN_FILES +=	include/asm-$(ARCH)/offsets.h.tmp \
		include/asm-$(ARCH)/offsets.h \
		arch/$(ARCH)/kernel/asm-offsets.s
