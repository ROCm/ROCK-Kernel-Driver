#
# ia64/Makefile
#
# This file is subject to the terms and conditions of the GNU General Public
# License.  See the file "COPYING" in the main directory of this archive
# for more details.
#
# Copyright (C) 1998-2000 by David Mosberger-Tang <davidm@hpl.hp.com>
#

NM := $(CROSS_COMPILE)nm -B
AWK := awk

export AWK

LINKFLAGS = -static -T arch/$(ARCH)/vmlinux.lds
AFLAGS += -Wa,-x 
AFLAGS_KERNEL := -mconstant-gp
EXTRA	=

CFLAGS := $(CFLAGS) -pipe $(EXTRA) -Wa,-x -ffixed-r13 -mfixed-range=f10-f15,f32-f127 \
	  -funwind-tables -falign-functions=32
# -frename-registers
CFLAGS_KERNEL := -mconstant-gp

ifeq ($(CONFIG_ITANIUM_ASTEP_SPECIFIC),y)
	CFLAGS += -ma-step
endif
ifeq ($(CONFIG_ITANIUM_BSTEP_SPECIFIC),y)
	CFLAGS += -mb-step
endif

ifdef CONFIG_IA64_GENERIC
	CORE_FILES      :=      arch/$(ARCH)/hp/hp.a	\
				arch/$(ARCH)/sn/sn.a	\
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

ifdef CONFIG_IA64_SGI_SN1
CFLAGS += -DBRINGUP
        SUBDIRS         :=      arch/$(ARCH)/sn/sn1	\
				arch/$(ARCH)/sn		\
				arch/$(ARCH)/sn/io	\
				arch/$(ARCH)/sn/fprom	\
				$(SUBDIRS)
        CORE_FILES      :=      arch/$(ARCH)/sn/sn.a	\
				arch/$(ARCH)/sn/io/sgiio.o\
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
	@$(MAKE) -C arch/$(ARCH)/kernel clean
	@$(MAKE) -C arch/$(ARCH)/tools clean
	@$(MAKEBOOT) clean

archmrproper:
	rm -f arch/$(ARCH)/vmlinux.lds
	@$(MAKE) -C arch/$(ARCH)/tools mrproper

archdep:
	@$(MAKEBOOT) dep

bootpfile:
	@$(MAKEBOOT) bootpfile
