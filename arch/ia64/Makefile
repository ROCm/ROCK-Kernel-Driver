#
# ia64/Makefile
#
# This file is subject to the terms and conditions of the GNU General Public
# License.  See the file "COPYING" in the main directory of this archive
# for more details.
#
# Copyright (C) 1998-2001 by David Mosberger-Tang <davidm@hpl.hp.com>
#

NM := $(CROSS_COMPILE)nm -B
AWK := awk

export AWK

LINKFLAGS = -static -T arch/$(ARCH)/vmlinux.lds
AFLAGS_KERNEL := -mconstant-gp
EXTRA	=

CFLAGS := $(CFLAGS) -pipe $(EXTRA) -ffixed-r13 -mfixed-range=f10-f15,f32-f127 \
	  -falign-functions=32
# -ffunction-sections
CFLAGS_KERNEL := -mconstant-gp

GCC_VERSION=$(shell $(CROSS_COMPILE)$(HOSTCC) -v 2>&1 | fgrep 'gcc version' | cut -f3 -d' ' | cut -f1 -d'.')

ifneq ($(GCC_VERSION),2)
	CFLAGS += -frename-registers --param max-inline-insns=2000
endif

ifeq ($(CONFIG_ITANIUM_BSTEP_SPECIFIC),y)
	CFLAGS += -mb-step
endif

ifdef CONFIG_IA64_GENERIC
	CORE_FILES      :=      arch/$(ARCH)/hp/hp.a	\
				arch/$(ARCH)/sn/sn.o	\
				arch/$(ARCH)/dig/dig.a	\
				arch/$(ARCH)/sn/io/sgiio.o \
				$(CORE_FILES)
	SUBDIRS		:=	arch/$(ARCH)/hp		\
				arch/$(ARCH)/sn/sn1	\
				arch/$(ARCH)/sn		\
				arch/$(ARCH)/dig	\
				arch/$(ARCH)/sn/io	\
				$(SUBDIRS)

else # !GENERIC

ifdef CONFIG_IA64_HP_SIM
        SUBDIRS         :=      arch/$(ARCH)/hp		\
                                $(SUBDIRS)
        CORE_FILES      :=      arch/$(ARCH)/hp/hp.a	\
                                $(CORE_FILES)
endif

ifdef CONFIG_IA64_SGI_SN
	CFLAGS		+= -DBRINGUP
        SUBDIRS         :=      arch/$(ARCH)/sn/kernel	\
				arch/$(ARCH)/sn/io	\
				arch/$(ARCH)/sn/fakeprom	\
				$(SUBDIRS)
        CORE_FILES      :=      arch/$(ARCH)/sn/kernel/sn.o	\
				arch/$(ARCH)/sn/io/sgiio.o	\
				$(CORE_FILES)
endif

ifdef CONFIG_IA64_SOFTSDV
        SUBDIRS         :=      arch/$(ARCH)/dig	\
				$(SUBDIRS)
        CORE_FILES      :=      arch/$(ARCH)/dig/dig.a	\
				$(CORE_FILES)
endif

ifdef CONFIG_IA64_DIG
        SUBDIRS         :=      arch/$(ARCH)/dig	\
				$(SUBDIRS)
        CORE_FILES      :=      arch/$(ARCH)/dig/dig.a	\
				$(CORE_FILES)
endif

endif # !GENERIC

ifdef CONFIG_IA32_SUPPORT
	SUBDIRS		:=	arch/$(ARCH)/ia32 $(SUBDIRS)
	CORE_FILES	:=	arch/$(ARCH)/ia32/ia32.o $(CORE_FILES)
endif

HEAD := arch/$(ARCH)/kernel/head.o arch/ia64/kernel/init_task.o

SUBDIRS := arch/$(ARCH)/tools arch/$(ARCH)/kernel arch/$(ARCH)/mm arch/$(ARCH)/lib $(SUBDIRS)
CORE_FILES := arch/$(ARCH)/kernel/kernel.o arch/$(ARCH)/mm/mm.o $(CORE_FILES)

LIBS := $(TOPDIR)/arch/$(ARCH)/lib/lib.a $(LIBS) \
	$(TOPDIR)/arch/$(ARCH)/lib/lib.a

MAKEBOOT = $(MAKE) -C arch/$(ARCH)/boot

vmlinux: arch/$(ARCH)/vmlinux.lds

arch/$(ARCH)/vmlinux.lds: arch/$(ARCH)/vmlinux.lds.S FORCE
	$(CPP) -D__ASSEMBLY__ -C -P -I$(HPATH) -I$(HPATH)/asm-$(ARCH) \
		-traditional arch/$(ARCH)/vmlinux.lds.S > $@

FORCE: ;

compressed: vmlinux
	$(OBJCOPY) --strip-all vmlinux vmlinux-tmp
	gzip vmlinux-tmp
	mv vmlinux-tmp.gz vmlinux.gz

rawboot:
	@$(MAKEBOOT) rawboot

#
# My boot writes directly to a specific disk partition, I doubt most
# people will want to do that without changes..
#
msb my-special-boot:
	@$(MAKEBOOT) msb

bootimage:
	@$(MAKEBOOT) bootimage

srmboot:
	@$(MAKEBOOT) srmboot

archclean:
	@$(MAKEBOOT) clean

archmrproper:
	rm -f arch/$(ARCH)/vmlinux.lds
	@$(MAKE) -C arch/$(ARCH)/tools mrproper

archdep:
	@$(MAKEBOOT) dep

bootpfile:
	@$(MAKEBOOT) bootpfile
