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
LDFLAGS_vmlinux	+= -static
MODFLAGS	+= -mlong-calls
LDFLAGS		:= -G 0

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
core-y		+= arch/mips/math-emu/

#
# Board-dependent options and extra files
#
ifdef CONFIG_ALGOR_P4032
core-y		+= arch/mips/algor/
LOADADDR	+= 0x80000000
endif

#
# DECstation family
#
ifdef CONFIG_DECSTATION
core-y		+= arch/mips/dec/
libs-y          += arch/mips/dec/prom/
LOADADDR	+= 0x80040000
endif

ifdef CONFIG_MIPS_ATLAS
core-y		+= arch/mips/mips-boards/atlas/ arch/mips/mips-boards/generic/
LOADADDR	+= 0x80100000
endif

ifdef CONFIG_MIPS_MALTA
core-y          += arch/mips/mips-boards/malta/ arch/mips/mips-boards/generic/
LOADADDR	+= 0x80100000
endif

#
# Acer PICA 61, Mips Magnum 4000 and Olivetti M700.
#
ifdef CONFIG_MIPS_JAZZ
core-y		+= arch/mips/jazz/
libs-y		+= arch/mips/arc/
LOADADDR	+= 0x80080000
endif

ifdef CONFIG_SNI_RM200_PCI
core-y		+= arch/mips/sni/
libs-y		+= arch/mips/arc/
LOADADDR	+= 0x80080000
endif

ifdef CONFIG_SGI_IP22
core-y		+= arch/mips/sgi/kernel/
libs-y		+= arch/mips/arc/
#
# Set LOADADDR to >= 0x88069000 if you want to leave space for symmon,
# 0x88002000 for production kernels.  Note that the value must be
# 8kb aligned or the handling of the current variable will break.
#
LOADADDR	+= 0x88002000
endif

#
# Baget/MIPS
#
ifdef CONFIG_BAGET_MIPS
core-y		+= arch/mips/baget/
libs-y		+= arch/mips/baget/prom/
LOADADDR	+= 0x80001000
endif

#
# NEC DDB Vrc-5074
#
ifdef CONFIG_DDB5074
core-y		+= arch/mips/ddb5074/
LOADADDR	+= 0x80080000
endif

#
#
# NEC DDB Vrc-5476
#
ifdef CONFIG_DDB5476
core-y		+= arch/mips/ddb5476/
LOADADDR	+= 0x80080000
endif

#
#
# NEC DDB Vrc-5477
#
ifdef CONFIG_DDB5477
core-y		+= arch/mips/ddb5xxx/common/ \
		   arch/mips/ddb5xxx/ddb5477/
LOADADDR	+= 0x80080000
endif

#
# Galileo EV64120 Board
#
ifdef CONFIG_MIPS_EV64120
core-y		+= arch/mips/galileo-boards/ev64120/
LOADADDR	+= 0x80100000
endif

#
# Galileo EV96100 Board
#
ifdef CONFIG_MIPS_EV96100
core-y		+= arch/mips/galileo-boards/ev96100/ \
		   arch/mips/galileo-boards/generic/
LOADADDR	+= 0x80100000
endif

#
# Momentum Ocelot board
#
ifdef CONFIG_MOMENCO_OCELOT
# The Ocelot setup.o must be linked early - it does the ioremap() for the
# mips_io_port_base.
core-y		+= arch/mips/gt64120/common/ \
		   arch/mips/gt64120/momenco_ocelot/
LOADADDR	+= 0x80100000
endif

#
# Philips Nino
#
ifdef CONFIG_NINO
core-y		+= arch/mips/philips/nino/
LOADADDR	+= 0x80000000
endif

#
# ITE 8172 eval board with QED 5231 CPU
#
ifdef CONFIG_MIPS_ITE8172
core-y		+= arch/mips/ite-boards/qed-4n-s01b/ \
		   arch/mips/ite-boards/generic/
LOADADDR	+= 0x80100000
endif

#
# Globespan IVR eval board with QED 5231 CPU
#
ifdef CONFIG_MIPS_IVR
core-y		+= arch/mips/ite-boards/ivr/ \
		   arch/mips/ite-boards/generic/
LOADADDR	+= 0x80100000
endif

#
# Au1000 eval board
#
ifdef CONFIG_MIPS_PB1000
core-y		+= arch/mips/au1000/pb1000/ \
		   arch/mips/au1000/common/
LOADADDR	+= 0x80100000
endif

#
# Choosing incompatible machines durings configuration will result in
# error messages during linking.  Select a default linkscript if
# none has been choosen above.
#

AFLAGS_vmlinux.lds.o := -DLOADADDR=$(LOADADDR)

HEAD := arch/mips/kernel/head.o arch/mips/kernel/init_task.o

SUBDIRS := arch/mips/tools

core-y		+= arch/mips/kernel/ arch/mips/mm/
libs-y		+= arch/mips/lib/lib.a

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
