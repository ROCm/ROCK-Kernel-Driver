#
# This file is subject to the terms and conditions of the GNU General Public
# License.  See the file "COPYING" in the main directory of this archive
# for more details.
#
# This file is included by the global makefile so that you can add your own
# architecture-specific flags and dependencies. Remember to do have actions
# for "archclean" and "archdep" for cleaning up and making dependencies for
# this architecture
#

#
# Select the object file format to substitute into the linker script.
#
CPP=$(CC) -E
ifdef CONFIG_CPU_LITTLE_ENDIAN
tool-prefix	= mips64el-linux-
else
tool-prefix	= mips64-linux-
endif

ifdef CONFIG_CROSSCOMPILE
CROSS_COMPILE  = $(tool-prefix)
endif

#
# The ELF GCC uses -G0 -mabicalls -fpic as default.  We don't need PIC
# code in the kernel since it only slows down the whole thing.  For the
# old GCC these options are just the defaults.  At some point we might
# make use of global pointer optimizations.
#
# The DECStation requires an ECOFF kernel for remote booting, other MIPS
# machines may also.  Since BFD is incredibly buggy with respect to
# crossformat linking we rely on the elf2ecoff tool for format conversion.
#
CFLAGS		+= -I $(TOPDIR)/include/asm $(CFLAGS)
CFLAGS		+= -mabi=64 -G 0 -mno-abicalls -fno-pic -Wa,--trap -pipe
LINKFLAGS	+= -G 0 -static # -N
MODFLAGS	+= -mlong-calls

ifdef CONFIG_REMOTE_DEBUG
CFLAGS		:= $(CFLAGS) -g
endif

#
# CPU-dependent compiler/assembler options for optimization.
#
ifdef CONFIG_CPU_R4300
CFLAGS		:= $(CFLAGS) -mcpu=r4300 -mips3
endif
ifdef CONFIG_CPU_R4X00
CFLAGS		:= $(CFLAGS) -mcpu=r4600 -mips3
endif
ifdef CONFIG_CPU_R5000
CFLAGS		:= $(CFLAGS) -mcpu=r8000 -mips4
endif
ifdef CONFIG_CPU_NEVADA
CFLAGS		:= $(CFLAGS) -mcpu=r8000 -mips3 -mmad
endif
ifdef CONFIG_CPU_R8000
CFLAGS		:= $(CFLAGS) -mcpu=r8000 -mips4
endif
ifdef CONFIG_CPU_R10000
CFLAGS		:= $(CFLAGS) -mcpu=r8000 -mips4
endif

#
# Board-dependent options and extra files
#
ifdef CONFIG_SGI_IP22
LIBS          += arch/mips64/sgi-ip22/ip22.a arch/mips64/arc/arclib.a
SUBDIRS       += arch/mips64/sgi-ip22 arch/mips64/arc
#
# Set LOADADDR to >= 0x88069000 if you want to leave space for symmon,
# 0x88004000 for production kernels.  Note that the value must be
# 16kb aligned or the handling of the current variable will break.
#
LOADADDR      += 0x88004000
endif

ifdef CONFIG_SGI_IP27
LIBS          += arch/mips64/sgi-ip27/ip27.a arch/mips64/arc/arclib.a
SUBDIRS       += arch/mips64/sgi-ip27 arch/mips64/arc
#
# Set LOADADDR to >= 0xc000000000300000 if you want to leave space for
# symmon, 0xc00000000001c000 for production kernels.  Note that the value
# must be 16kb aligned or the handling of the current variable will break.
#
#LOADADDR      += 0xa80000000001c000
ifdef CONFIG_MAPPED_KERNEL
LOADADDR      += 0xc001c000
else
LOADADDR      += 0x8001c000
endif
endif

#
# Some machines like the Indy need 32-bit ELF binaries for booting purposes.
# Other need ECOFF, so we build a 32-bit ELF binary for them which we then
# convert to ECOFF using elf2ecoff.
#
ifdef CONFIG_BOOT_ELF32
CFLAGS += -Wa,-32
LINKFLAGS += -T arch/mips64/ld.script.elf32
endif
#
# The 64-bit ELF tools are pretty broken so at this time we generate 64-bit
# ELF files from 32-bit files by conversion.
#
ifdef CONFIG_BOOT_ELF64
CFLAGS += -Wa,-32
LINKFLAGS += -T arch/mips64/ld.script.elf32
#AS += -64
#LD += -m elf64bmip
#LINKFLAGS += -T arch/mips64/ld.script.elf64
endif

LINKFLAGS += -Ttext $(LOADADDR)

HEAD := arch/mips64/kernel/head.o arch/mips64/kernel/init_task.o

SUBDIRS := $(addprefix arch/mips64/, tools) $(SUBDIRS) $(addprefix arch/mips64/, kernel mm lib)
CORE_FILES := arch/mips64/kernel/kernel.o arch/mips64/mm/mm.o $(CORE_FILES)
LIBS := arch/mips64/lib/lib.a $(LIBS)

MAKEBOOT = $(MAKE) -C arch/$(ARCH)/boot

ifdef CONFIG_CPU_LITTLE_ENDIAN
64bit-bfd = elf64-littlemips
else
64bit-bfd = elf64-bigmips
endif

vmlinux: arch/mips64/ld.script.elf32
arch/mips64/ld.script.elf32: arch/mips64/ld.script.elf32.S
	$(CPP) -C -P -I$(HPATH) -imacros $(HPATH)/asm-mips64/sn/mapped_kernel.h -Umips arch/mips64/ld.script.elf32.S > arch/mips64/ld.script.elf32

ifdef CONFIG_MAPPED_KERNEL
vmlinux.64: vmlinux
	$(OBJCOPY) -O $(64bit-bfd) --change-addresses=0xbfffffff40000000 $< $@
else
vmlinux.64: vmlinux
	$(OBJCOPY) -O $(64bit-bfd) --change-addresses=0xa7ffffff80000000 $< $@
endif

zImage: vmlinux
	@$(MAKEBOOT) zImage

compressed: zImage

zdisk: vmlinux
	@$(MAKEBOOT) zdisk

archclean:
	@$(MAKEBOOT) clean
	$(MAKE) -C arch/$(ARCH)/kernel clean
	$(MAKE) -C arch/$(ARCH)/tools clean
	rm -f vmlinux.64 arch/$(ARCH)/ld.script.elf32

archmrproper:
	@$(MAKEBOOT) mrproper
	$(MAKE) -C arch/$(ARCH)/tools mrproper

archdep:
	if [ ! -f $(TOPDIR)/include/asm-$(ARCH)/offset.h ]; then \
	 touch $(TOPDIR)/include/asm-$(ARCH)/offset.h; \
	fi;
	@$(MAKEBOOT) dep
