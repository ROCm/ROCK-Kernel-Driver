#
# This file is subject to the terms and conditions of the GNU General Public
# License.  See the file "COPYING" in the main directory of this archive
# for more details.
#
# Copyright (C) 1994, 1995, 1996 by Ralf Baechle
# DECStation modifications by Paul M. Antoine, 1996
#
# This file is included by the global makefile so that you can add your own
# architecture-specific flags and dependencies. Remember to do have actions
# for "archclean" and "archdep" for cleaning up and making dependencies for
# this architecture
#

#
# Select the object file format to substitute into the linker script.
#
ifdef CONFIG_CPU_LITTLE_ENDIAN
tool-prefix	= mipsel-linux-
else
tool-prefix	= mips-linux-
endif

ifdef CONFIG_CROSSCOMPILE
CROSS_COMPILE  = $(tool-prefix)
endif

#
# GCC uses -G0 -mabicalls -fpic as default.  We don't want PIC in the kernel
# code since it only slows down the whole thing.  At some point we might make
# use of global pointer optimizations but their use of $28 conflicts with
# the current pointer optimization.
#
# The DECStation requires an ECOFF kernel for remote booting, other MIPS
# machines may also.  Since BFD is incredibly buggy with respect to
# crossformat linking we rely on the elf2ecoff tool for format conversion.
#
GCCFLAGS	:= -G 0 -mno-abicalls -fno-pic
LINKFLAGS	+= -static
MODFLAGS	+= -mlong-calls
LD		:= $(LD) -G 0

ifdef CONFIG_REMOTE_DEBUG
CFLAGS		:= $(CFLAGS) -g
endif

#
# CPU-dependent compiler/assembler options for optimization.
#
ifdef CONFIG_CPU_R3000
GCCFLAGS	+= -mcpu=r3000 -mips1
endif
ifdef CONFIG_CPU_R6000
GCCFLAGS	+= -mcpu=r6000 -mips2 -Wa,--trap
endif
ifdef CONFIG_CPU_R4300
GCCFLAGS	+= -mcpu=r4300 -mips2 -Wa,--trap
endif
ifdef CONFIG_CPU_VR41XX
GCCFLAGS	+= -mcpu=r4600 -mips2 -Wa,--trap
endif
ifdef CONFIG_CPU_R4X00
GCCFLAGS	+= -mcpu=r4600 -mips2 -Wa,--trap
endif
ifdef CONFIG_CPU_MIPS32
GCCFLAGS	+= -mcpu=r4600 -mips2 -Wa,--trap
endif
ifdef CONFIG_CPU_MIPS64
GCCFLAGS	+= -mcpu=r4600 -mips2 -Wa,--trap
endif
ifdef CONFIG_CPU_R5000
GCCFLAGS	+= -mcpu=r5000 -mips2 -Wa,--trap
endif
ifdef CONFIG_CPU_R5432
GCCFLAGS        += -mcpu=r5000 -mips2 -Wa,--trap
endif
ifdef CONFIG_CPU_NEVADA
# Cannot use -mmad with currently recommended tools
GCCFLAGS	+= -mcpu=r5000 -mips2 -Wa,--trap
endif
ifdef CONFIG_CPU_RM7000
GCCFLAGS	+= -mcpu=r5000 -mips2 -Wa,--trap
endif
ifdef CONFIG_CPU_SB1
GCCFLAGS	+= -mcpu=sb1 -mips2 -Wa,--trap
endif

GCCFLAGS	+= -pipe

CFLAGS		:= -I $(TOPDIR)/include/asm/gcc $(CFLAGS) $(GCCFLAGS)
AFLAGS		+= $(GCCFLAGS)
ASFLAGS		+= $(GCCFLAGS)

#
# We unconditionally build the math emulator
#
CORE_FILES	+=arch/mips/math-emu/fpu_emulator.o
SUBDIRS		+=arch/mips/math-emu

#
# Board-dependent options and extra files
#
ifdef CONFIG_ALGOR_P4032
CORE_FILES    += arch/mips/algor/algor.o
SUBDIRS       += arch/mips/algor
LOADADDR      += 0x80000000
endif

#
# DECstation family
#
ifdef CONFIG_DECSTATION
CORE_FILES    += arch/mips/dec/dec.o
SUBDIRS       += arch/mips/dec arch/mips/dec/prom
LIBS          += arch/mips/dec/prom/rexlib.a
LOADADDR      += 0x80040000
endif

ifdef CONFIG_MIPS_ATLAS
LIBS          += arch/mips/mips-boards/atlas/atlas.o arch/mips/mips-boards/generic/mipsboards.o
SUBDIRS       += arch/mips/mips-boards/generic arch/mips/mips-boards/atlas
LOADADDR      += 0x80100000
endif

ifdef CONFIG_MIPS_MALTA
LIBS          += arch/mips/mips-boards/malta/malta.o arch/mips/mips-boards/generic/mipsboards.o
SUBDIRS       += arch/mips/mips-boards/malta arch/mips/mips-boards/generic
LOADADDR      += 0x80100000
endif

#
# Acer PICA 61, Mips Magnum 4000 and Olivetti M700.
#
ifdef CONFIG_MIPS_JAZZ
CORE_FILES    += arch/mips/jazz/jazz.o
SUBDIRS       += arch/mips/jazz arch/mips/arc
LIBS          += arch/mips/arc/arclib.a
LOADADDR      += 0x80080000
endif

ifdef CONFIG_SNI_RM200_PCI
CORE_FILES    += arch/mips/sni/sni.o
SUBDIRS       += arch/mips/sni arch/mips/arc
LIBS          += arch/mips/arc/arclib.a
LOADADDR      += 0x80080000
endif

ifdef CONFIG_SGI_IP22
CORE_FILES    += arch/mips/sgi/kernel/ip22-kern.o
LIBS          += arch/mips/arc/arclib.a
SUBDIRS       += arch/mips/sgi/kernel arch/mips/arc
#
# Set LOADADDR to >= 0x88069000 if you want to leave space for symmon,
# 0x88002000 for production kernels.  Note that the value must be
# 8kb aligned or the handling of the current variable will break.
#
LOADADDR      += 0x88002000
endif

#
# Baget/MIPS
#
ifdef CONFIG_BAGET_MIPS
SUBDIRS       += arch/mips/baget arch/mips/baget/prom
LIBS          += arch/mips/baget/baget.a arch/mips/baget/prom/bagetlib.a
LOADADDR      += 0x80001000
endif

#
# NEC DDB Vrc-5074
#
ifdef CONFIG_DDB5074
SUBDIRS       += arch/mips/ddb5074
LIBS          += arch/mips/ddb5074/ddb5074.a
LOADADDR      += 0x80080000
endif

#
#
# NEC DDB Vrc-5476
#
ifdef CONFIG_DDB5476
SUBDIRS       += arch/mips/ddb5476
LIBS          += arch/mips/ddb5476/ddb5476.a
LOADADDR      += 0x80080000
endif

#
#
# NEC DDB Vrc-5477
#
ifdef CONFIG_DDB5477
SUBDIRS       += arch/mips/ddb5xxx/common arch/mips/ddb5xxx/ddb5477
LIBS          += arch/mips/ddb5xxx/common/ddb5xxx.o \
                 arch/mips/ddb5xxx/ddb5477/ddb5477.o
LOADADDR      += 0x80080000
endif

#
# Galileo EV64120 Board
#
ifdef CONFIG_MIPS_EV64120
LIBS          += arch/mips/galileo-boards/ev64120/ev64120.o
SUBDIRS       += arch/mips/galileo-boards/ev64120
LOADADDR      += 0x80100000
endif

#
# Galileo EV96100 Board
#
ifdef CONFIG_MIPS_EV96100
LIBS          += arch/mips/galileo-boards/ev96100/ev96100.o arch/mips/galileo-boards/generic/galboards.o
SUBDIRS       += arch/mips/galileo-boards/generic arch/mips/galileo-boards/ev96100
LOADADDR      += 0x80100000
endif

#
# Momentum Ocelot board
#
ifdef CONFIG_MOMENCO_OCELOT
# The Ocelot setup.o must be linked early - it does the ioremap() for the
# mips_io_port_base.
CORE_FILES    += arch/mips/gt64120/common/gt64120.o arch/mips/gt64120/momenco_ocelot/momenco_ocelot.o
SUBDIRS       += arch/mips/gt64120/common arch/mips/gt64120/momenco_ocelot
LOADADDR      += 0x80100000
endif

#
# Philips Nino
#
ifdef CONFIG_NINO
CORE_FILES    += arch/mips/philips/nino/nino.o
SUBDIRS       += arch/mips/philips/nino
LOADADDR      += 0x80000000
endif

#
# ITE 8172 eval board with QED 5231 CPU
#
ifdef CONFIG_MIPS_ITE8172
LIBS          += arch/mips/ite-boards/qed-4n-s01b/ite.o arch/mips/ite-boards/generic/it8172.o
SUBDIRS       += arch/mips/ite-boards/generic arch/mips/ite-boards/qed-4n-s01b
LOADADDR      += 0x80100000
endif

#
# Globespan IVR eval board with QED 5231 CPU
#
ifdef CONFIG_MIPS_IVR
LIBS          += arch/mips/ite-boards/ivr/ivr.o arch/mips/ite-boards/generic/it8172.o
SUBDIRS       += arch/mips/ite-boards/generic arch/mips/ite-boards/ivr
LOADADDR      += 0x80100000
endif

#
# Au1000 eval board
#
ifdef CONFIG_MIPS_PB1000
LIBS          += arch/mips/au1000/pb1000/pb1000.o arch/mips/au1000/common/au1000.o
SUBDIRS       += arch/mips/au1000/pb1000 arch/mips/au1000/common
LOADADDR      += 0x80100000
endif

#
# Choosing incompatible machines durings configuration will result in
# error messages during linking.  Select a default linkscript if
# none has been choosen above.
#
vmlinux: arch/$(ARCH)/ld.script

arch/$(ARCH)/ld.script: arch/$(ARCH)/ld.script.in arch/$(ARCH)/Makefile
	sed -e 's/@@LOADADDR@@/$(LOADADDR)/' <$< >$@
LINKFLAGS	+= -T arch/$(ARCH)/ld.script

HEAD := arch/mips/kernel/head.o arch/mips/kernel/init_task.o

SUBDIRS := $(addprefix arch/mips/, tools) $(SUBDIRS) $(addprefix arch/mips/, kernel mm lib)
CORE_FILES := arch/mips/kernel/kernel.o arch/mips/mm/mm.o $(CORE_FILES)
LIBS := arch/mips/lib/lib.a $(LIBS)

ifdef CONFIG_BAGET_MIPS

BAGETBOOT = $(MAKE) -C arch/$(ARCH)/baget

balo:  vmlinux
	$(BAGETBOOT) balo

endif

ifdef CONFIG_MIPS_EV64120
GALILEOBOOT = $(MAKE) -C arch/$(ARCH)/galileo-boards/ev64120

gboot: vmlinux
	$(MAKE) -C arch/$(ARCH)/galileo-boards/ev64120/compressed 
endif

MAKEBOOT = $(MAKE) -C arch/$(ARCH)/boot

vmlinux.ecoff: vmlinux
	@$(MAKEBOOT) $@

archclean:
	@$(MAKEBOOT) clean
	rm -f arch/$(ARCH)/ld.script
	$(MAKE) -C arch/$(ARCH)/tools clean
	$(MAKE) -C arch/mips/baget clean

archmrproper:
	@$(MAKEBOOT) mrproper
	$(MAKE) -C arch/$(ARCH)/tools mrproper

archdep:
	if [ ! -f $(TOPDIR)/include/asm-$(ARCH)/offset.h ]; then \
	 touch $(TOPDIR)/include/asm-$(ARCH)/offset.h; \
	fi;
	@$(MAKEBOOT) dep
