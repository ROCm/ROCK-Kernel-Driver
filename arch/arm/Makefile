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

OBJCOPY		:= $(CROSS_COMPILE)objcopy -O binary -R .note -R .comment -S
CPP		:= $(CC) -E
LINKFLAGS	:= -p -X -T arch/arm/vmlinux.lds
ARCHCC		:= $(word 1,$(CC))

AFLAGS		+= -mno-fpu
CFLAGS_PIPE	:= -pipe
CFLAGS		:= $(CFLAGS) $(CFLAGS_PIPE) -msoft-float

ifdef CONFIG_FRAME_POINTER
CFLAGS		:= $(CFLAGS:-fomit-frame-pointer=)
endif

ifdef CONFIG_DEBUG_INFO
CFLAGS		+= -g
endif

GZFLAGS		= -9

# Ensure this is ld "2.9.4" or later
NEW_LINKER	:= $(shell $(LD) --gc-sections --version >/dev/null 2>&1; echo $$?)

ifneq ($(NEW_LINKER),0)
dummy:;		@echo '*** ${VERSION}.${PATCHLEVEL} kernels no longer build correctly with old versions of binutils.'
		@echo '*** Please upgrade your binutils to 2.9.5.'
		@false
endif

# GCC 2.7 uses different options to later compilers; sort out which we have
NEW_GCC		:= $(shell $(CC) --version 2>&1 | grep '^2\.7' > /dev/null; echo $$?)

#
# select flags depending on the compiler
#
ifneq ($(NEW_GCC),0)
CFLAGS			+= -mshort-load-bytes
CFLAGS_PROC_CPU_26	:= -mcpu=arm3 -mapcs-26 -Os
CFLAGS_PROC_CPU_32v3	:= -march=armv3
CFLAGS_PROC_CPU_32v4	:= -march=armv4
else
CFLAGS			+= -DNO_TEXT_SECTIONS
CFLAGS_PROC_CPU_26	:= -m3
CFLAGS_PROC_CPU_32v3	:= -m6
CFLAGS_PROC_CPU_32v4	:= -m6
endif

#
# Select CPU dependent flags
#
ifeq ($(CONFIG_CPU_26),y)
 PROCESSOR	 = armo
 TEXTADDR	 = 0x02080000
 CFLAGS		+= $(CFLAGS_PROC_CPU_26)
 AFLAGS		+= -mapcs-26
endif

ifeq ($(CONFIG_CPU_32),y)
 PROCESSOR	 = armv
 TEXTADDR	 = 0xC0008000
 ifeq ($(CONFIG_CPU_32v4),y)
  CFLAGS	+= $(CFLAGS_PROC_CPU_32v4)
  AFLAGS	+= -mapcs-32 -marmv4
 else
  CFLAGS	+= $(CFLAGS_PROC_CPU_32v3)
  AFLAGS	+= -mapcs-32 -marmv3m
 endif

 opt-$(CONFIG_CPU_ARM6)		:= -mtune=arm6
 opt-$(CONFIG_CPU_ARM7)		:= -mtune=arm7
 opt-$(CONFIG_CPU_ARM720)	:= -mtune=arm7tdmi
 opt-$(CONFIG_CPU_ARM920)	:= -mtune=arm9tdmi
 opt-$(CONFIG_CPU_SA110)	:= -mtune=strongarm110
 opt-$(CONFIG_CPU_SA1100)	:= -mtune=strongarm110

 ifneq ($(NEW_GCC),0)
 CFLAGS	+= $(opt-y)
 endif
endif

LIBGCC		:= $(shell $(CC) $(CFLAGS) --print-libgcc-file-name)

export	LIBGCC MACHINE PROCESSOR TEXTADDR GZFLAGS

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
MACHINE		 = sa1100
endif

ifeq ($(CONFIG_ARCH_L7200),y)
MACHINE		 = l7200
endif

ifeq ($(CONFIG_ARCH_INTEGRATOR),y)
MACHINE		 = integrator
endif

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
SUBDIRS		+= arch/arm/kernel arch/arm/mm arch/arm/lib \
		   arch/arm/nwfpe
CORE_FILES	:= arch/arm/kernel/kernel.o arch/arm/mm/mm.o $(CORE_FILES)
LIBS		:= arch/arm/lib/lib.o arch/arm/lib/lib.a $(LIBS) $(LIBGCC)

ifeq ($(CONFIG_NWFPE),y)
LIBS		:= arch/arm/nwfpe/math-emu.o $(LIBS)
endif

ifeq ($(CONFIG_ARCH_ACORN),y)
SUBDIRS		+= drivers/acorn
DRIVERS		+= drivers/acorn/block/acorn-block.a
DRIVERS		+= drivers/acorn/char/acorn-char.o
DRIVERS		+= drivers/acorn/net/acorn-net.o
DRIVERS		+= drivers/acorn/scsi/acorn-scsi.a
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

#
# Configuration targets.  Use these to select a
# configuration for your architecture
CFGS=	a5k_config		ebsa110_config		\
	footbridge_config	rpc_config		\
	brutus_config		victor_config		\
	empeg_config		thinclient_config	\
	assabet_config		lart_config		\
	cerf_config		lusl7200_config 

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
