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

KERNELLOAD	=0xc0000000

ifeq ($(shell uname -m),ppc)
CHECKS		= checks
endif

ASFLAGS		=
LINKFLAGS	= -T arch/ppc/vmlinux.lds -Ttext $(KERNELLOAD) -Bstatic
CPPFLAGS	:= $(CPPFLAGS) -D__powerpc__
CFLAGS		:= $(CFLAGS) -D__powerpc__ -fsigned-char \
		-msoft-float -pipe -ffixed-r2 -Wno-uninitialized \
		-mmultiple -mstring
CPP		= $(CC) -E $(CFLAGS)

ifdef CONFIG_4xx
CFLAGS := $(CFLAGS) -mcpu=403
endif

ifdef CONFIG_8xx
CFLAGS := $(CFLAGS) -mcpu=860 -I../8xx_io
endif

ifdef CONFIG_PPC64BRIDGE
CFLAGS := $(CFLAGS) -Wa,-mppc64bridge
endif

ifdef CONFIG_4xx
  HEAD := arch/ppc/kernel/head_4xx.o
else
  ifdef CONFIG_8xx
    HEAD := arch/ppc/kernel/head_8xx.o
  else
    HEAD := arch/ppc/kernel/head.o
  endif
endif

ARCH_SUBDIRS = arch/ppc/kernel arch/ppc/mm arch/ppc/lib
SUBDIRS := $(SUBDIRS) $(ARCH_SUBDIRS)
ARCHIVES := arch/ppc/kernel/kernel.o arch/ppc/mm/mm.o arch/ppc/lib/lib.o $(ARCHIVES)
CORE_FILES := arch/ppc/kernel/kernel.o arch/ppc/mm/mm.o arch/ppc/lib/lib.o $(CORE_FILES)

ifdef CONFIG_MATH_EMULATION
SUBDIRS += arch/ppc/math-emu
ARCHIVES += arch/ppc/math-emu/math-emu.o
CORE_FILES += arch/ppc/math-emu/math-emu.o
endif

ifdef CONFIG_XMON
SUBDIRS += arch/ppc/xmon
CORE_FILES += arch/ppc/xmon/x.o
endif

MAKEBOOT = $(MAKE) -C arch/$(ARCH)/boot
MAKECOFFBOOT = $(MAKE) -C arch/$(ARCH)/coffboot
MAKECHRPBOOT = $(MAKE) -C arch/$(ARCH)/chrpboot
MAKEMBXBOOT = $(MAKE) -C arch/$(ARCH)/mbxboot
MAKETREEBOOT = $(MAKE) -C arch/$(ARCH)/treeboot

ifdef CONFIG_8xx
SUBDIRS += arch/ppc/8xx_io
DRIVERS += arch/ppc/8xx_io/8xx_io.o
endif

ifdef CONFIG_8260
SUBDIRS += arch/ppc/8260_io
DRIVERS += arch/ppc/8260_io/8260_io.o
endif

ifdef CONFIG_APUS
SUBDIRS += arch/ppc/amiga
ARCHIVES += arch/ppc/amiga/amiga.o
CORE_FILES += arch/ppc/amiga/amiga.o
endif

checks:
	@$(MAKE) -C arch/$(ARCH)/kernel checks

BOOT_TARGETS = zImage znetboot.initrd zImage.initrd

ifdef CONFIG_4xx
$(BOOT_TARGETS): $(CHECKS) vmlinux
	@$(MAKETREEBOOT) $@
endif

ifdef CONFIG_8xx
$(BOOT_TARGETS): $(CHECKS) vmlinux
	@$(MAKECOFFBOOT) $@
	@$(MAKEMBXBOOT) $@
endif

ifdef CONFIG_6xx
ifndef CONFIG_8260
$(BOOT_TARGETS): $(CHECKS) vmlinux
	@$(MAKECOFFBOOT) $@
	@$(MAKEBOOT) $@
	@$(MAKECHRPBOOT) $@

znetboot: $(CHECKS) vmlinux
ifdef CONFIG_ALL_PPC
ifdef CONFIG_SMP
	cp -f vmlinux /tftpboot/vmlinux.smp
else
	cp -f vmlinux /tftpboot/vmlinux
endif
endif
	@$(MAKECOFFBOOT) $@
	@$(MAKEBOOT) $@
	@$(MAKECHRPBOOT) $@
else
# 8260 is custom 6xx
$(BOOT_TARGETS): $(CHECKS) vmlinux
	@$(MAKECOFFBOOT) $@
	@$(MAKEMBXBOOT) $@
endif
endif

ifdef CONFIG_PPC64BRIDGE
$(BOOT_TARGETS): $(CHECKS) vmlinux
	@$(MAKECOFFBOOT) $@
	@$(MAKEBOOT) $@
	@$(MAKECHRPBOOT) $@

znetboot: $(CHECKS) vmlinux
	cp -f vmlinux /tftpboot/vmlinux.64
	@$(MAKECOFFBOOT) $@
	@$(MAKEBOOT) $@
	@$(MAKECHRPBOOT) $@
endif

.PHONY: clean_config
clean_config:
	rm -f .config arch/ppc/defconfig

gemini_config: clean_config
	cp -f arch/ppc/configs/gemini_defconfig arch/ppc/defconfig

pmac_config: clean_config
	cp -f arch/ppc/configs/pmac_defconfig arch/ppc/defconfig

prep_config: clean_config
	cp -f arch/ppc/configs/prep_defconfig arch/ppc/defconfig

chrp_config: clean_config
	cp -f arch/ppc/configs/chrp_defconfig arch/ppc/defconfig

common_config: clean_config
	cp -f arch/ppc/configs/common_defconfig arch/ppc/defconfig

mbx_config: clean_config
	cp -f arch/ppc/configs/mbx_defconfig arch/ppc/defconfig

apus_config: clean_config
	cp -f arch/ppc/configs/apus_defconfig arch/ppc/defconfig

oak_config: clean_config
	cp -f arch/ppc/configs/oak_defconfig arch/ppc/defconfig

walnut_config: clean_config
	cp -f arch/ppc/configs/walnut_defconfig arch/ppc/defconfig

rpxlite_config: clean_config
	cp -f arch/ppc/configs/rpxlite_defconfig arch/ppc/defconfig

rpxcllf_config: clean_config
	cp -f arch/ppc/configs/rpxcllf_defconfig arch/ppc/defconfig

bseip_config: clean_config
	cp -f arch/ppc/configs/bseip_defconfig arch/ppc/defconfig

est8260_config: clean_config
	cp -f arch/ppc/configs/est8260_defconfig arch/ppc/defconfig

archclean:
	rm -f arch/ppc/kernel/{mk_defs,ppc_defs.h,find_name,checks}
	@$(MAKECOFFBOOT) clean
	@$(MAKEBOOT) clean
	@$(MAKECHRPBOOT) clean
	@$(MAKEMBXBOOT) clean
	@$(MAKETREEBOOT) clean

archmrproper:

archdep:
	$(MAKEBOOT) fastdep
	$(MAKECHRPBOOT) fastdep
