#
# arch/arm/Makefile
#
# This file is included by the global makefile so that you can add your own
# architecture-specific flags and dependencies. Remember to do have actions
# for "archclean" and "archdep" for cleaning up and making dependencies for
# this architecture
#
# This file is subject to the terms and conditions of the GNU General Public
# License.  See the file "COPYING" in the main directory of this archive
# for more details.
#
# Copyright (C) 1995-2000 by Russell King

LINKFLAGS	:=-p -X -T arch/arm/vmlinux.lds
GZFLAGS		:=-9
CFLAGS		+=-fno-common -pipe

ifdef CONFIG_FRAME_POINTER
CFLAGS		:=$(CFLAGS:-fomit-frame-pointer=)
endif

ifdef CONFIG_DEBUG_INFO
CFLAGS		+=-g
endif

# Ensure this is ld "2.9.4" or later
NEW_LINKER	:= $(shell $(LD) --gc-sections --version >/dev/null 2>&1; echo $$?)

ifneq ($(NEW_LINKER),0)
dummy:;		@echo '*** ${VERSION}.${PATCHLEVEL} kernels no longer build correctly with old versions of binutils.'
		@echo '*** Please upgrade your binutils to 2.9.5.'
		@false
endif

# Select CPU dependent flags.  Note that order of declaration is important;
# the options further down the list override previous items.
#
apcs-$(CONFIG_CPU_26)		:=-mapcs-26 -mcpu=arm3 -Os
apcs-$(CONFIG_CPU_32)		:=-mapcs-32

arch-$(CONFIG_CPU_32v3)		:=-march=armv3
arch-$(CONFIG_CPU_32v4)		:=-march=armv4
arch-$(CONFIG_CPU_32v5)		:=-march=armv5

proc-$(CONFIG_CPU_32v3)		:=-marmv3m
proc-$(CONFIG_CPU_32v4)		:=-marmv4
proc-$(CONFIG_CPU_32v5)		:=-marmv5

tune-$(CONFIG_CPU_ARM610)	:=-mtune=arm610
tune-$(CONFIG_CPU_ARM710)	:=-mtune=arm710
tune-$(CONFIG_CPU_ARM720T)	:=-mtune=arm7tdmi
tune-$(CONFIG_CPU_ARM920T)	:=-mtune=arm9tdmi
tune-$(CONFIG_CPU_SA110)	:=-mtune=strongarm110
tune-$(CONFIG_CPU_SA1100)	:=-mtune=strongarm1100

CFLAGS		+= $(apcs-y) $(arch-y) $(tune-y) -mshort-load-bytes -msoft-float
AFLAGS		+= $(apcs-y) $(proc-y) -mno-fpu

LIBGCC		:= $(shell $(CC) $(CFLAGS) --print-libgcc-file-name)

ifeq ($(CONFIG_CPU_26),y)
PROCESSOR	 = armo
TEXTADDR	 = 0x02080000
endif

ifeq ($(CONFIG_CPU_32),y)
PROCESSOR	 = armv
TEXTADDR	 = 0xC0008000
endif

ifeq ($(CONFIG_ARCH_ARCA5K),y)
MACHINE		 = arc
endif

ifeq ($(CONFIG_ARCH_RPC),y)
MACHINE		 = rpc
endif

ifeq ($(CONFIG_ARCH_EBSA110),y)
MACHINE		 = ebsa110
endif

ifeq ($(CONFIG_ARCH_CLPS7500),y)
MACHINE		 = clps7500
INCDIR		 = cl7500
endif

ifeq ($(CONFIG_FOOTBRIDGE),y)
MACHINE		 = footbridge
INCDIR		 = ebsa285
endif

ifeq ($(CONFIG_ARCH_CO285),y)
TEXTADDR	 = 0x60008000
MACHINE		 = footbridge
INCDIR		 = ebsa285
endif

ifeq ($(CONFIG_ARCH_NEXUSPCI),y)
MACHINE		 = nexuspci
endif

ifeq ($(CONFIG_ARCH_SHARK),y)
MACHINE		 = shark
endif

ifeq ($(CONFIG_ARCH_SA1100),y)
ifeq ($(CONFIG_SA1111),y)
# SA1111 DMA bug: we don't want the kernel to live in precious DMA-able memory
TEXTADDR	 = 0xc0208000
endif
MACHINE		 = sa1100
endif

ifeq ($(CONFIG_ARCH_L7200),y)
MACHINE		 = l7200
endif

ifeq ($(CONFIG_ARCH_INTEGRATOR),y)
MACHINE		 = integrator
endif

ifeq ($(CONFIG_ARCH_CLPS711X),y)
TEXTADDR	 = 0xc0018000
MACHINE		 = clps711x
endif

export	LIBGCC MACHINE PROCESSOR TEXTADDR GZFLAGS

# Only set INCDIR if its not already defined above
# Grr, ?= doesn't work as all the other assignment operators do.  Make bug?
ifeq ($(origin INCDIR), undefined)
INCDIR		:= $(MACHINE)
endif

# If we have a machine-specific directory, then include it in the build.
MACHDIR		:= arch/arm/mach-$(MACHINE)
ifeq ($(MACHDIR),$(wildcard $(MACHDIR)))
SUBDIRS		+= $(MACHDIR)
CORE_FILES	:= $(MACHDIR)/$(MACHINE).o $(CORE_FILES)
endif

HEAD		:= arch/arm/kernel/head-$(PROCESSOR).o \
		   arch/arm/kernel/init_task.o
SUBDIRS		+= arch/arm/kernel arch/arm/mm arch/arm/lib arch/arm/nwfpe
CORE_FILES	:= arch/arm/kernel/kernel.o arch/arm/mm/mm.o $(CORE_FILES)
LIBS		:= arch/arm/lib/lib.o arch/arm/lib/lib.a $(LIBS) $(LIBGCC)

ifeq ($(CONFIG_NWFPE),y)
LIBS		:= arch/arm/nwfpe/math-emu.o $(LIBS)
endif

ifeq ($(CONFIG_ARCH_CLPS7500),y)
SUBDIRS		+= drivers/acorn/char
DRIVERS		+= drivers/acorn/char/acorn-char.o
endif

MAKEBOOT	 = $(MAKE) -C arch/$(ARCH)/boot

# The following is a hack to get 'constants.h' up
# to date before starting compilation

$(patsubst %, _dir_%, $(SUBDIRS)) init/main.o init/version.o : \
	include/asm-arm/mach-types.h

$(patsubst %, _dir_%, $(SUBDIRS)) : constants

include/asm-arm/mach-types.h: arch/arm/tools/mach-types  \
			      arch/arm/tools/gen-mach-types
	@awk -f arch/arm/tools/gen-mach-types arch/arm/tools/mach-types > $@

constants: dummy
	@$(MAKE) -C arch/arm/lib constants.h

symlinks: archsymlinks

archsymlinks:
	$(RM) include/asm-arm/arch include/asm-arm/proc
	(cd include/asm-arm; ln -sf arch-$(INCDIR) arch; ln -sf proc-$(PROCESSOR) proc)

vmlinux: arch/arm/vmlinux.lds

arch/arm/vmlinux.lds: arch/arm/vmlinux-$(PROCESSOR).lds.in dummy
	@sed 's/TEXTADDR/$(TEXTADDR)/' <$< >$@

arch/arm/kernel arch/arm/mm arch/arm/lib: dummy
	$(MAKE) CFLAGS="$(CFLAGS) $(CFLAGS_KERNEL)" $(subst $@, _dir_$@, $@)

bzImage zImage zinstall Image bootpImage install: vmlinux
	@$(MAKEBOOT) $@

archmrproper:
	$(RM) include/asm-arm/arch include/asm-arm/proc

archclean:
	@$(MAKEBOOT) clean
	$(RM) arch/arm/lib/constants.h arch/arm/vmlinux.lds
	$(RM) include/asm-arm/mach-types.h

archdep: archsymlinks
	@$(MAKEBOOT) dep

# My testing targets (that short circuit a few dependencies)
zImg:;	@$(MAKEBOOT) zImage
Img:;	@$(MAKEBOOT) Image
i:;	@$(MAKEBOOT) install
zi:;	@$(MAKEBOOT) zinstall
bp:;	@$(MAKEBOOT) bootpImage

#
# Configuration targets.  Use these to select a
# configuration for your architecture
CFGS=	a5k_config		ebsa110_config		\
	footbridge_config	rpc_config		\
	brutus_config		victor_config		\
	empeg_config		graphicsclient_config	\
	assabet_config		lart_config		\
	cerf_config		lusl7200_config 	\
	sherman_config		pangolin_config

$(CFGS):
	@( \
	CFG=$(@:_config=); \
	if [ -f arch/arm/def-configs/$$CFG ]; then \
	  $(RM) arch/arm/defconfig; \
	  cp arch/arm/def-configs/$$CFG arch/arm/defconfig; \
	  echo "*** Default configuration for $$CFG installed"; \
	  echo "*** Next, you may run 'make oldconfig'"; \
	else \
	  echo "$$CFG does not exist"; \
	fi; \
	)
