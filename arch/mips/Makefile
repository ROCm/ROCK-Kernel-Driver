#
# This file is subject to the terms and conditions of the GNU General Public
# License.  See the file "COPYING" in the main directory of this archive
# for more details.
#
# Copyright (C) 1994, 95, 96, 2003 by Ralf Baechle
# DECStation modifications by Paul M. Antoine, 1996
# Copyright (C) 2002, 2003  Maciej W. Rozycki
#
# This file is included by the global makefile so that you can add your own
# architecture-specific flags and dependencies. Remember to do have actions
# for "archclean" cleaning up for this architecture.
#

#
# Select the object file format to substitute into the linker script.
#
ifdef CONFIG_CPU_LITTLE_ENDIAN
32bit-tool-prefix	= mipsel-linux-
64bit-tool-prefix	= mips64el-linux-
32bit-bfd		= elf32-tradlittlemips
64bit-bfd		= elf64-tradlittlemips
else
32bit-tool-prefix	= mips-linux-
64bit-tool-prefix	= mips64-linux-
32bit-bfd		= elf32-tradbigmips
64bit-bfd		= elf64-tradbigmips
endif

ifdef CONFIG_MIPS32
gcc-abi			= 32
gas-abi			= 32
tool-prefix		= $(32bit-tool-prefix)
UTS_MACHINE		:= mips
endif
ifdef CONFIG_MIPS64
gcc-abi			= 64
gas-abi			= 32
tool-prefix		= $(64bit-tool-prefix)
UTS_MACHINE		:= mips64
endif

ifdef CONFIG_CROSSCOMPILE
CROSS_COMPILE		:= $(tool-prefix)
endif

#
# GCC uses -G 0 -mabicalls -fpic as default.  We don't want PIC in the kernel
# code since it only slows down the whole thing.  At some point we might make
# use of global pointer optimizations but their use of $28 conflicts with
# the current pointer optimization.
#
# The DECStation requires an ECOFF kernel for remote booting, other MIPS
# machines may also.  Since BFD is incredibly buggy with respect to
# crossformat linking we rely on the elf2ecoff tool for format conversion.
#
cflags-y			:= -I $(TOPDIR)/include/asm/gcc
cflags-y			+= -G 0 -mno-abicalls -fno-pic -pipe
cflags-y			+= $(call check_gcc, -finline-limit=100000,)
LDFLAGS_vmlinux			+= -G 0 -static -n
MODFLAGS			+= -mlong-calls

cflags-$(CONFIG_SB1XXX_CORELIS)	+= -mno-sched-prolog -fno-omit-frame-pointer

check_warning = $(shell if $(CC) $(1) -c -o /dev/null -xc /dev/null > /dev/null 2>&1; then echo "$(1)"; else echo "$(2)"; fi)

#
# Use: $(call set_gccflags,<cpu0>,<isa0>,<cpu1>,<isa1>,<isa2>)
#
# <cpu0>,<isa0> -- preferred CPU and ISA designations (may require
#                  recent tools)
# <cpu1>,<isa1> -- fallback CPU and ISA designations (have to work
#                  with up to the oldest supported tools)
# <isa2>        -- an ISA designation used as an ABI selector for
#                  gcc versions that do not support "-mabi=32"
#                  (depending on the CPU type, either "mips1" or
#                  "mips2")
#
set_gccflags = $(shell \
while :; do \
	cpu=$(1); isa=-$(2); \
	for gcc_opt in -march= -mcpu=; do \
		$(CC) $$gcc_opt$$cpu $$isa -S -o /dev/null \
			-xc /dev/null > /dev/null 2>&1 && \
			break 2; \
	done; \
	cpu=$(3); isa=-$(4); \
	for gcc_opt in -march= -mcpu=; do \
		$(CC) $$gcc_opt$$cpu $$isa -S -o /dev/null \
			-xc /dev/null > /dev/null 2>&1 && \
			break 2; \
	done; \
	break; \
done; \
gcc_abi=-mabi=$(gcc-abi); gcc_cpu=$$cpu; \
if $(CC) $$gcc_abi -S -o /dev/null -xc /dev/null > /dev/null 2>&1; then \
	gcc_isa=$$isa; \
else \
	gcc_abi=; gcc_isa=-$(5); \
fi; \
gas_abi=-Wa,-$(gcc-abi); gas_cpu=$$cpu; gas_isa=-Wa,$$isa; \
while :; do \
	for gas_opt in -Wa,-march= -Wa,-mcpu=; do \
		$(CC) $$gas_abi $$gas_opt$$cpu $$gas_isa -Wa,-Z -c \
			-o /dev/null -xassembler /dev/null > /dev/null 2>&1 && \
			break 2; \
	done; \
	gas_abi=; gas_opt=; gas_cpu=; gas_isa=; \
	break; \
done; \
if test "$(gcc-abi)" != "$(gas-abi)"; then \
	gas_abi="-Wa,-$(gas-abi) -Wa,-mgp$(gcc-abi)"; \
fi; \
if test "$$gcc_opt" = -march= && test -n "$$gcc_abi"; then \
	$(CC) $$gcc_abi $$gcc_opt$$gcc_cpu -S -o /dev/null \
		-xc /dev/null > /dev/null 2>&1 && \
		gcc_isa=; \
fi; \
echo $$gcc_abi $$gcc_opt$$gcc_cpu $$gcc_isa $$gas_abi $$gas_opt$$gas_cpu $$gas_isa)

#
# CPU-dependent compiler/assembler options for optimization.
#
cflags-$(CONFIG_CPU_R3000)	+= \
			$(call set_gccflags,r3000,mips1,r3000,mips1,mips1)

cflags-$(CONFIG_CPU_TX39XX)	+= \
			$(call set_gccflags,r3900,mips1,r3000,mips1,mips1)

cflags-$(CONFIG_CPU_R6000)	+= \
			$(call set_gccflags,r6000,mips2,r6000,mips2,mips2) \
			-Wa,--trap

cflags-$(CONFIG_CPU_R4300)	+= \
			$(call set_gccflags,r4300,mips3,r4300,mips3,mips2) \
			-Wa,--trap

cflags-$(CONFIG_CPU_VR41XX)	+= \
			$(call set_gccflags,r4100,mips3,r4600,mips3,mips2) \
			-Wa,--trap

cflags-$(CONFIG_CPU_R4X00)	+= \
			$(call set_gccflags,r4600,mips3,r4600,mips3,mips2) \
			-Wa,--trap

cflags-$(CONFIG_CPU_MIPS32)	+= \
			$(call set_gccflags,mips32,mips32,r4600,mips3,mips2) \
			-Wa,--trap

cflags-$(CONFIG_CPU_MIPS64)	+= \
			$(call set_gccflags,mips64,mips64,r4600,mips3,mips2) \
			-Wa,--trap

cflags-$(CONFIG_CPU_R5000)	+= \
			$(call set_gccflags,r5000,mips4,r5000,mips4,mips2) \
			-Wa,--trap 

cflags-$(CONFIG_CPU_R5432)	+= \
			$(call set_gccflags,r5400,mips4,r5000,mips4,mips2) \
			-Wa,--trap

cflags-$(CONFIG_CPU_NEVADA)	+= \
			$(call set_gccflags,rm5200,mips4,r5000,mips4,mips2) \
			-Wa,--trap
#			$(call check_gcc,-mmad,)

cflags-$(CONFIG_CPU_RM7000)	+= \
			$(call set_gccflags,rm7000,mips4,r5000,mips4,mips2) \
			-Wa,--trap

cflags-$(CONFIG_CPU_RM9000)	+= \
			$(call set_gccflags,rm9000,mips4,r5000,mips4,mips2) \
			-Wa,--trap

cflags-$(CONFIG_CPU_SB1)	+= \
			$(call set_gccflags,sb1,mips64,r5000,mips4,mips2) \
			-Wa,--trap

cflags-$(CONFIG_CPU_R8000)	+= \
			$(call set_gccflags,r8000,mips4,r8000,mips4,mips2) \
			-Wa,--trap

cflags-$(CONFIG_CPU_R10000)	+= \
			$(call set_gccflags,r10000,mips4,r8000,mips4,mips2) \
			-Wa,--trap

ifdef CONFIG_CPU_SB1
ifdef CONFIG_SB1_PASS_1_WORKAROUNDS
MODFLAGS	+= -msb1-pass1-workarounds
endif
endif

#
# ramdisk/initrd support
# You need a compressed ramdisk image, named
# CONFIG_EMBEDDED_RAMDISK_IMAGE. Relative pathnames 
# are relative to arch/mips/ramdisk/.
#
core-$(CONFIG_EMBEDDED_RAMDISK)	+= arch/mips/ramdisk/

#
# Firmware support
#
libs-$(CONFIG_ARC)		+= arch/mips/arc/
libs-$(CONFIG_SIBYTE_CFE)	+= arch/mips/sibyte/cfe/

#
# Board-dependent options and extra files
#

#
# Acer PICA 61, Mips Magnum 4000 and Olivetti M700.
#
core-$(CONFIG_MACH_JAZZ)	+= arch/mips/jazz/
cflags-$(CONFIG_MACH_JAZZ)	+= -Iinclude/asm-mips/mach-jazz
load-$(CONFIG_MACH_JAZZ)	+= 0x80080000

#
# Common Alchemy Au1x00 stuff
#
core-$(CONFIG_SOC_AU1X00)	+= arch/mips/au1000/common/
cflags-$(CONFIG_SOC_AU1X00)	+= -Iinclude/asm-mips/mach-au1x00

#
# AMD Alchemy Pb1000 eval board
#
libs-$(CONFIG_MIPS_PB1000)	+= arch/mips/au1000/pb1000/
cflags-$(CONFIG_MIPS_PB1000)	+= -Iinclude/asm-mips/mach-pb1x00
load-$(CONFIG_MIPS_PB1000)	+= 0x80100000

#
# AMD Alchemy Pb1100 eval board
#
libs-$(CONFIG_MIPS_PB1100)	+= arch/mips/au1000/pb1100/
cflags-$(CONFIG_MIPS_PB1100)	+= -Iinclude/asm-mips/mach-pb1x00
load-$(CONFIG_MIPS_PB1100)	+= 0x80100000

#
# AMD Alchemy Pb1500 eval board
#
libs-$(CONFIG_MIPS_PB1500)	+= arch/mips/au1000/pb1500/
cflags-$(CONFIG_MIPS_PB1500)	+= -Iinclude/asm-mips/mach-pb1x00
load-$(CONFIG_MIPS_PB1500)	+= 0x80100000

#
# AMD Alchemy Pb1550 eval board
#
libs-$(CONFIG_MIPS_PB1550)	+= arch/mips/au1000/pb1550/
cflags-$(CONFIG_MIPS_PB1550)	+= -Iinclude/asm-mips/mach-pb1x00
load-$(CONFIG_MIPS_PB1550)	+= 0x80100000

#
# AMD Alchemy Db1000 eval board
#
libs-$(CONFIG_MIPS_DB1000)	+= arch/mips/au1000/db1x00/
cflags-$(CONFIG_MIPS_DB1000)	+= -Iinclude/asm-mips/mach-db1x00
load-$(CONFIG_MIPS_DB1000)	+= 0x80100000

#
# AMD Alchemy Db1100 eval board
#
libs-$(CONFIG_MIPS_DB1100)	+= arch/mips/au1000/db1x00/
cflags-$(CONFIG_MIPS_DB1100)	+= -Iinclude/asm-mips/mach-db1x00
load-$(CONFIG_MIPS_DB1100)	+= 0x80100000

#
# AMD Alchemy Db1500 eval board
#
libs-$(CONFIG_MIPS_DB1500)	+= arch/mips/au1000/db1x00/
cflags-$(CONFIG_MIPS_DB1500)	+= -Iinclude/asm-mips/mach-db1x00
load-$(CONFIG_MIPS_DB1500)	+= 0x80100000

#
# AMD Alchemy Bosporus eval board
#
libs-$(CONFIG_MIPS_BOSPORUS)	+= arch/mips/au1000/db1x00/
cflags-$(CONFIG_MIPS_BOSPORUS)	+= -Iinclude/asm-mips/mach-db1x00
load-$(CONFIG_MIPS_BOSPORUS)	+= 0x80100000

#
# AMD Alchemy Mirage eval board
#
libs-$(CONFIG_MIPS_MIRAGE)	+= arch/mips/au1000/db1x00/
cflags-$(CONFIG_MIPS_MIRAGE)	+= -Iinclude/asm-mips/mach-db1x00
load-$(CONFIG_MIPS_MIRAGE)	+= 0x80100000

#
# 4G-Systems eval board
#
libs-$(CONFIG_MIPS_MTX1)	+= arch/mips/au1000/mtx-1/
load-$(CONFIG_MIPS_MTX1)	+= 0x80100000

#
# MyCable eval board
#
libs-$(CONFIG_MIPS_XXS1500)	+= arch/mips/au1000/xxs1500/
load-$(CONFIG_MIPS_XXS1500)	+= 0x80100000

#
# Baget/MIPS
#
libs-$(CONFIG_BAGET_MIPS)	+= arch/mips/baget/ arch/mips/baget/prom/
load-$(CONFIG_BAGET_MIPS)	+= 0x80001000

#
# Cobalt Server
#
core-$(CONFIG_MIPS_COBALT)	+= arch/mips/cobalt/
load-$(CONFIG_MIPS_COBALT)	+= 0x80080000

#
# DECstation family
#
core-$(CONFIG_MACH_DECSTATION)	+= arch/mips/dec/
cflags-$(CONFIG_MACH_DECSTATION)+= -Iinclude/asm-mips/mach-dec
libs-$(CONFIG_MACH_DECSTATION)	+= arch/mips/dec/prom/
load-$(CONFIG_MACH_DECSTATION)	+= 0x80040000
CLEAN_FILES			+= drivers/tc/lk201-map.c

#
# Galileo EV64120 Board
#
core-$(CONFIG_MIPS_EV64120)	+= arch/mips/gt64120/ev64120/
core-$(CONFIG_MIPS_EV64120)	+= arch/mips/gt64120/common/
cflags-$(CONFIG_MIPS_EV64120)	+= -Iinclude/asm-mips/mach-ev64120
load-$(CONFIG_MIPS_EV64120)	+= 0x80100000

#
# Galileo EV96100 Board
#
core-$(CONFIG_MIPS_EV96100)	+= arch/mips/galileo-boards/ev96100/
cflags-$(CONFIG_MIPS_EV96100)	+= -Iinclude/asm-mips/mach-ev96100
load-$(CONFIG_MIPS_EV96100)	+= 0x80100000

#
# Globespan IVR eval board with QED 5231 CPU
#
core-$(CONFIG_ITE_BOARD_GEN)	+= arch/mips/ite-boards/generic/
core-$(CONFIG_MIPS_IVR)		+= arch/mips/ite-boards/ivr/
load-$(CONFIG_MIPS_IVR)		+= 0x80100000

#
# HP LaserJet
#
core-$(CONFIG_HP_LASERJET)	+= arch/mips/hp-lj/
load-$(CONFIG_HP_LASERJET)	+= 0x80030000

#
# ITE 8172 eval board with QED 5231 CPU
#
core-$(CONFIG_MIPS_ITE8172)	+= arch/mips/ite-boards/qed-4n-s01b/
load-$(CONFIG_MIPS_ITE8172)	+= 0x80100000

#
# For all MIPS, Inc. eval boards
#
core-$(CONFIG_MIPS_BOARDS_GEN)	+= arch/mips/mips-boards/generic/

#
# MIPS Atlas board
#
core-$(CONFIG_MIPS_ATLAS)	+= arch/mips/mips-boards/atlas/
cflags-$(CONFIG_MIPS_ATLAS)	+= -Iinclude/asm-mips/mach-atlas
cflags-$(CONFIG_MIPS_ATLAS)	+= -Iinclude/asm-mips/mach-mips
load-$(CONFIG_MIPS_ATLAS)	+= 0x80100000

#
# MIPS Malta board
#
core-$(CONFIG_MIPS_MALTA)	+= arch/mips/mips-boards/malta/
cflags-$(CONFIG_MIPS_MALTA)	+= -Iinclude/asm-mips/mach-mips
load-$(CONFIG_MIPS_MALTA)	+= 0x80100000

#
# MIPS SEAD board
#
core-$(CONFIG_MIPS_SEAD)	+= arch/mips/mips-boards/sead/
load-$(CONFIG_MIPS_SEAD)	+= 0x80100000

#
# Momentum Ocelot board
#
# The Ocelot setup.o must be linked early - it does the ioremap() for the
# mips_io_port_base.
#
core-$(CONFIG_MOMENCO_OCELOT)	+= arch/mips/gt64120/common/ \
				   arch/mips/gt64120/momenco_ocelot/
cflags-$(CONFIG_MOMENCO_OCELOT)	+= -Iinclude/asm-mips/mach-ocelot
load-$(CONFIG_MOMENCO_OCELOT)	+= 0x80100000

#
# Momentum Ocelot-G board
#
# The Ocelot-G setup.o must be linked early - it does the ioremap() for the
# mips_io_port_base.
#
core-$(CONFIG_MOMENCO_OCELOT_G)	+= arch/mips/momentum/ocelot_g/
load-$(CONFIG_MOMENCO_OCELOT_G)	+= 0x80100000

#
# Momentum Ocelot-C and -CS boards
#
# The Ocelot-C[S] setup.o must be linked early - it does the ioremap() for the
# mips_io_port_base.
core-$(CONFIG_MOMENCO_OCELOT_C)	+= arch/mips/momentum/ocelot_c/
load-$(CONFIG_MOMENCO_OCELOT_C)	+= 0x80100000

#
# Momentum Jaguar ATX
#
core-$(CONFIG_MOMENCO_JAGUAR_ATX)	+= arch/mips/momentum/jaguar_atx/
cflags-$(CONFIG_MOMENCO_JAGUAR_ATX)	+= -Iinclude/asm-mips/mach-ja
#ifdef CONFIG_JAGUAR_DMALOW
#load-$(CONFIG_MOMENCO_JAGUAR_ATX)	+= 0x88000000
#else
load-$(CONFIG_MOMENCO_JAGUAR_ATX)	+= 0x80100000
#endif

#
# NEC DDB
#
core-$(CONFIG_DDB5XXX_COMMON)	+= arch/mips/ddb5xxx/common/

#
# NEC DDB Vrc-5074
#
core-$(CONFIG_DDB5074)		+= arch/mips/ddb5xxx/ddb5074/
load-$(CONFIG_DDB5074)		+= 0x80080000

#
# NEC DDB Vrc-5476
#
core-$(CONFIG_DDB5476)		+= arch/mips/ddb5xxx/ddb5476/
load-$(CONFIG_DDB5476)		+= 0x80080000

#
# NEC DDB Vrc-5477
#
core-$(CONFIG_DDB5477)		+= arch/mips/ddb5xxx/ddb5477/
load-$(CONFIG_DDB5477)		+= 0x80100000

core-$(CONFIG_LASAT)		+= arch/mips/lasat/
cflags-$(CONFIG_LASAT)		+= -Iinclude/asm-mips/mach-lasat
load-$(CONFIG_LASAT)		+= 0x80000000

#
# NEC Osprey (vr4181) board
#
core-$(CONFIG_NEC_OSPREY)	+= arch/mips/vr4181/common/ \
				   arch/mips/vr4181/osprey/
load-$(CONFIG_NEC_OSPREY)	+= 0x80002000

#
# Common VR41xx
#
core-$(CONFIG_MACH_VR41XX)	+= arch/mips/vr41xx/common/
cflags-$(CONFIG_MACH_VR41XX)	+= -Iinclude/asm-mips/mach-vr41xx

#
# NEC Eagle/Hawk (VR4122/VR4131) board
#
core-$(CONFIG_NEC_EAGLE)	+= arch/mips/vr41xx/nec-eagle/
load-$(CONFIG_NEC_EAGLE)	+= 0x80000000

#
# ZAO Networks Capcella (VR4131)
#
core-$(CONFIG_ZAO_CAPCELLA)	+= arch/mips/vr41xx/zao-capcella/
load-$(CONFIG_ZAO_CAPCELLA)	+= 0x80000000

#
# Victor MP-C303/304 (VR4122)
#
core-$(CONFIG_VICTOR_MPC30X)	+= arch/mips/vr41xx/victor-mpc30x/
load-$(CONFIG_VICTOR_MPC30X)	+= 0x80001000

#
# IBM WorkPad z50 (VR4121)
#
core-$(CONFIG_IBM_WORKPAD)	+= arch/mips/vr41xx/ibm-workpad/
load-$(CONFIG_IBM_WORKPAD)	+= 0x80004000

#
# CASIO CASSIPEIA E-55/65 (VR4111)
#
core-$(CONFIG_CASIO_E55)	+= arch/mips/vr41xx/casio-e55/
load-$(CONFIG_CASIO_E55)	+= 0x80004000

#
# TANBAC TB0226 Mbase (VR4131)
#
core-$(CONFIG_TANBAC_TB0226)	+= arch/mips/vr41xx/tanbac-tb0226/
load-$(CONFIG_TANBAC_TB0226)	+= 0x80000000

#
# TANBAC TB0229 VR4131DIMM (VR4131)
#
core-$(CONFIG_TANBAC_TB0229)	+= arch/mips/vr41xx/tanbac-tb0229/
load-$(CONFIG_TANBAC_TB0229)	+= 0x80000000

#
# SGI IP22 (Indy/Indigo2)
#
# Set the load address to >= 0x88069000 if you want to leave space for symmon,
# 0x80002000 for production kernels.  Note that the value must be aligned to
# a multiple of the kernel stack size or the handling of the current variable
# will break so for 64-bit kernels we have to raise the start address by 8kb.
#
core-$(CONFIG_SGI_IP22)		+= arch/mips/sgi-ip22/
cflags-$(CONFIG_SGI_IP22)	+= -Iinclude/asm-mips/mach-ip22
ifdef CONFIG_MIPS32
load-$(CONFIG_SGI_IP22)		+= 0x88002000
endif
ifdef CONFIG_MIPS64
load-$(CONFIG_SGI_IP22)		+= 0x88004000
endif

#
# SGI-IP27 (Origin200/2000)
#
# Set the load address to >= 0xc000000000300000 if you want to leave space for
# symmon, 0xc00000000001c000 for production kernels.  Note that the value
# must be 16kb aligned or the handling of the current variable will break.
#
ifdef CONFIG_SGI_IP27
core-$(CONFIG_SGI_IP27)		+= arch/mips/sgi-ip27/
cflags-$(CONFIG_SGI_IP27)	+= -Iinclude/asm-mips/mach-ip27
#load-$(CONFIG_SGI_IP27)	+= 0xa80000000001c000
ifdef CONFIG_MAPPED_KERNEL
load-$(CONFIG_SGI_IP27)		+= 0xc001c000
else
load-$(CONFIG_SGI_IP27)		+= 0x8001c000
endif
endif

#
# SGI-IP32 (O2)
#
# Set the load address to >= 80069000 if you want to leave space for symmon,
# 0x80002000 for production kernels.  Note that the value must be aligned to
# a multiple of the kernel stack size or the handling of the current variable
# will break so for 64-bit kernels we have to raise the start address by 8kb.
#
core-$(CONFIG_SGI_IP32)		+= arch/mips/sgi-ip32/
cflags-$(CONFIG_SGI_IP32)	+= -Iinclude/asm-mips/mach-ip32
ifdef CONFIG_MIPS32
load-$(CONFIG_SGI_IP32)		+= 0x80002000
endif
ifdef CONFIG_MIPS64
load-$(CONFIG_SGI_IP32)		+= 0x80004000
endif

#
# Sibyte SB1250 SOC
#
# This is a LIB so that it links at the end, and initcalls are later
# the sequence; but it is built as an object so that modules don't get
# removed (as happens, even if they have __initcall/module_init)
#
core-$(CONFIG_SIBYTE_BCM112X)	+= arch/mips/sibyte/sb1250/

core-$(CONFIG_SIBYTE_SB1250)	+= arch/mips/sibyte/sb1250/

#
# Sibyte BCM91120x (Carmel) board
# Sibyte BCM91120C (CRhine) board
# Sibyte BCM91125C (CRhone) board
# Sibyte BCM91125E (Rhone) board
# Sibyte SWARM board
#
libs-$(CONFIG_SIBYTE_CARMEL)	+= arch/mips/sibyte/swarm/
load-$(CONFIG_SIBYTE_CARMEL)	:= 0x80100000
libs-$(CONFIG_SIBYTE_CRHINE)	+= arch/mips/sibyte/swarm/
load-$(CONFIG_SIBYTE_CRHINE)	:= 0x80100000
libs-$(CONFIG_SIBYTE_CRHONE)	+= arch/mips/sibyte/swarm/
load-$(CONFIG_SIBYTE_CRHONE)	:= 0x80100000
libs-$(CONFIG_SIBYTE_RHONE)	+= arch/mips/sibyte/swarm/
load-$(CONFIG_SIBYTE_RHONE)	:= 0x80100000
libs-$(CONFIG_SIBYTE_SENTOSA)	+= arch/mips/sibyte/swarm/
load-$(CONFIG_SIBYTE_SENTOSA)	:= 0x80100000
libs-$(CONFIG_SIBYTE_SWARM)	+= arch/mips/sibyte/swarm/
load-$(CONFIG_SIBYTE_SWARM)	:= 0x80100000

#
# SNI RM200 PCI
#
core-$(CONFIG_SNI_RM200_PCI)	+= arch/mips/sni/
cflags-$(CONFIG_SNI_RM200_PCI)	+= -Iinclude/asm-mips/mach-rm200
load-$(CONFIG_SNI_RM200_PCI)	+= 0x80600000

#
# Toshiba JMR-TX3927 board
#
core-$(CONFIG_TOSHIBA_JMR3927)	+= arch/mips/jmr3927/rbhma3100/ \
				   arch/mips/jmr3927/common/
load-$(CONFIG_TOSHIBA_JMR3927)	+= 0x80050000

#
# Toshiba RBTX4927 board or
# Toshiba RBTX4937 board
#
core-$(CONFIG_TOSHIBA_RBTX4927)	+= arch/mips/tx4927/toshiba_rbtx4927/
core-$(CONFIG_TOSHIBA_RBTX4927)	+= arch/mips/tx4927/common/
load-$(CONFIG_TOSHIBA_RBTX4927)	+= 0x80020000

cflags-y			+= -Iinclude/asm-mips/mach-generic
drivers-$(CONFIG_PCI)		+= arch/mips/pci/

ifdef CONFIG_MIPS32
build-bfd		= $(32bit-bfd)
cflags-y		+= $(32bit-isa-y)
endif
ifdef CONFIG_MIPS64
build-bfd		= $(64bit-bfd)
cflags-y		+= $(64bit-isa-y)
endif

ifdef CONFIG_MIPS32
ifdef CONFIG_CPU_LITTLE_ENDIAN
JIFFIES			= jiffies_64
else
JIFFIES			= jiffies_64 + 4
endif
else
JIFFIES			= jiffies_64
endif

#
# Some machines like the Indy need 32-bit ELF binaries for booting purposes.
# Other need ECOFF, so we build a 32-bit ELF binary for them which we then
# convert to ECOFF using elf2ecoff.
#
# The 64-bit ELF tools are pretty broken so at this time we generate 64-bit
# ELF files from 32-bit files by conversion.
#
#AS += -64
#LDFLAGS += -m elf64bmip

#
# Choosing incompatible machines durings configuration will result in
# error messages during linking.  Select a default linkscript if
# none has been choosen above.
#

AFLAGS_vmlinux.lds.o := \
	-D"LOADADDR=$(load-y)" \
	-D"JIFFIES=$(JIFFIES)" \
	-imacros $(srctree)/include/asm-$(ARCH)/sn/mapped_kernel.h

AFLAGS		+= $(cflags-y)
CFLAGS		+= $(cflags-y)

LDFLAGS			+= --oformat $(32bit-bfd)

head-y := arch/mips/kernel/head.o arch/mips/kernel/init_task.o

libs-y			+= arch/mips/lib/
libs-$(CONFIG_MIPS32)	+= arch/mips/lib-32/
libs-$(CONFIG_MIPS64)	+= arch/mips/lib-64/

core-y			+= arch/mips/kernel/ arch/mips/mm/ arch/mips/math-emu/
core-$(CONFIG_MIPS32)	+= arch/mips/mm-32/
core-$(CONFIG_MIPS64)	+= arch/mips/mm-64/

ifdef CONFIG_BAGET_MIPS

BAGETBOOT = $(MAKE) -C arch/$(ARCH)/baget

balo: vmlinux
	$(BAGETBOOT) balo

endif

ifdef CONFIG_LASAT
rom.bin rom.sw: vmlinux
	$(call descend,arch/mips/lasat/image,$@)
endif

ifdef CONFIG_MAPPED_KERNEL
vmlinux.64: vmlinux
	$(OBJCOPY) -O $(64bit-bfd) --remove-section=.reginfo \
		--change-addresses=0xc000000080000000 $< $@
else
vmlinux.64: vmlinux
	$(OBJCOPY) -O $(64bit-bfd) --remove-section=.reginfo \
		--change-addresses=0xa800000080000000 $< $@
endif

makeboot =$(Q)$(MAKE) -f scripts/Makefile.build obj=arch/mips/boot $(1)

ifdef CONFIG_SGI_IP27
all:	vmlinux.64
endif

ifdef CONFIG_SNI_RM200_PCI
all:	vmlinux.ecoff
endif

vmlinux.ecoff vmlinux.rm200: vmlinux
	+@$(call makeboot,$@)

vmlinux.srec: vmlinux
	+@$(call makeboot,$@)

CLEAN_FILES += vmlinux.ecoff \
	       vmlinux.srec \
	       vmlinux.rm200.tmp \
	       vmlinux.rm200

archclean:
	@$(MAKE) -f scripts/Makefile.clean obj=arch/mips/boot
	@$(MAKE) -f scripts/Makefile.clean obj=arch/mips/baget
	@$(MAKE) -f scripts/Makefile.clean obj=arch/mips/lasat

# Generate <asm/offset.h 
#
# The default rule is suffering from funny problems on MIPS so we using our
# own ...
#
# ---------------------------------------------------------------------------

define filechk_gen-asm-offset.h
	(set -e; \
	 echo "#ifndef _ASM_OFFSET_H"; \
	 echo "#define _ASM_OFFSET_H"; \
	 echo "/*"; \
	 echo " * DO NOT MODIFY."; \
	 echo " *"; \
	 echo " * This file was generated by arch/$(ARCH)/Makefile"; \
	 echo " *"; \
	 echo " */"; \
	 echo ""; \
	 sed -ne "/^@@@/s///p"; \
	 echo "#endif /* _ASM_OFFSET_H */" )
endef

define filechk_gen-asm-reg.h
	(set -e; \
	 echo "#ifndef _ASM_REG_H"; \
	 echo "#define _ASM_REG_H"; \
	 echo "/*"; \
	 echo " * DO NOT MODIFY."; \
	 echo " *"; \
	 echo " * This file was generated by arch/$(ARCH)/Makefile"; \
	 echo " *"; \
	 echo " */"; \
	 echo ""; \
	 sed -ne "/^@@@/s///p"; \
	 echo "#endif /* _ASM_REG_H */" )
endef

prepare: include/asm-$(ARCH)/offset.h \
	 include/asm-$(ARCH)/reg.h

arch/$(ARCH)/kernel/offset.s: include/asm include/linux/version.h \
				   include/config/MARKER

include/asm-$(ARCH)/offset.h: arch/$(ARCH)/kernel/offset.s
	$(call filechk,gen-asm-offset.h)
include/asm-$(ARCH)/reg.h: arch/$(ARCH)/kernel/reg.s
	$(call filechk,gen-asm-reg.h)

CLEAN_FILES += include/asm-$(ARCH)/offset.h.tmp \
	       include/asm-$(ARCH)/offset.h \
	       include/asm-$(ARCH)/reg.h.tmp \
	       include/asm-$(ARCH)/reg.h \
	       vmlinux.64 \
	       vmlinux.ecoff
