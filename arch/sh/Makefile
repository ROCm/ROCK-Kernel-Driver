# $Id: Makefile,v 1.6 2000/06/10 03:03:52 gniibe Exp $
#
# This file is subject to the terms and conditions of the GNU General Public
# License.  See the file "COPYING" in the main directory of this archive
# for more details.
#
# Copyright (C) 1999  Kaz Kojima
#
# This file is included by the global makefile so that you can add your own
# architecture-specific flags and dependencies. Remember to do have actions
# for "archclean" and "archdep" for cleaning up and making dependencies for
# this architecture
#

ifdef CONFIG_CPU_LITTLE_ENDIAN
CFLAGS		+= -ml
AFLAGS		+= -ml
# LDFLAGS_vmlinux	+= -EL
LDFLAGS		:= -EL
else
CFLAGS		+= -mb
AFLAGS		+= -mb
# LDFLAGS_vmlinux	+= -EB
LDFLAGS		:= -EB
endif

OBJCOPYFLAGS	:= -O binary -R .note -R .comment -R .stab -R .stabstr -S

MODFLAGS	+=

#
#

ifdef CONFIG_CPU_SH3
CFLAGS		+= -m3
AFLAGS		+= -m3
endif
ifdef CONFIG_CPU_SH4
CFLAGS		+= -m4 -mno-implicit-fp
AFLAGS		+= -m4 -mno-implicit-fp
endif

#
# Choosing incompatible machines durings configuration will result in
# error messages during linking.  Select a default linkscript if
# none has been choosen above.
#
LINKSCRIPT    = arch/sh/vmlinux.lds
LDFLAGS_vmlinux     += -T $(word 1,$(LINKSCRIPT)) -e _stext

ifdef LOADADDR
LDFLAGS_vmlinux     += -Ttext $(word 1,$(LOADADDR))
endif

#
CFLAGS		+= -pipe

HEAD := arch/sh/kernel/head.o arch/sh/kernel/init_task.o

SUBDIRS := $(SUBDIRS) $(addprefix arch/sh/, kernel mm lib)
CORE_FILES := arch/sh/kernel/kernel.o arch/sh/mm/mm.o $(CORE_FILES)
LIBGCC := $(shell $(CC) $(CFLAGS) -print-libgcc-file-name)
LIBS := $(TOPDIR)/arch/sh/lib/lib.a $(LIBS) $(TOPDIR)/arch/sh/lib/lib.a \
	$(LIBGCC)

MAKEBOOT = $(MAKE) -C arch/$(ARCH)/boot

ifneq ($(CONFIG_SH_GENERIC)$(CONFIG_SH_STB1_HARP)$(CONFIG_SH_STB1_OVERDRIVE),)
SUBDIRS += arch/sh/stboards
CORE_FILES += arch/sh/stboards/stboards.o
endif

vmlinux: arch/sh/vmlinux.lds

arch/sh/vmlinux.lds: arch/sh/vmlinux.lds.S FORCE
	$(CPP) $(CPPFLAGS) -traditional -C -P -Ush $< > $@

FORCE: ;

zImage: vmlinux
	@$(MAKEBOOT) zImage

compressed: zImage

zdisk: vmlinux
	@$(MAKEBOOT) zdisk

archclean:
	@$(MAKEBOOT) clean
	$(MAKE) -C arch/$(ARCH)/kernel clean
	$(MAKE) -C arch/$(ARCH)/stboards clean
#	$(MAKE) -C arch/$(ARCH)/tools clean

archmrproper:
	rm -f arch/sh/vmlinux.lds
