# 
# Copyright (C) 2002 Jeff Dike (jdike@karaya.com)
# Licensed under the GPL
#

ARCH_DIR = arch/um
OS := $(shell uname -s)

# Recalculate MODLIB to reflect the EXTRAVERSION changes (via KERNELRELEASE)
# The way the toplevel Makefile is written EXTRAVERSION is not supposed
# to be changed outside the toplevel Makefile, but recalculating MODLIB is
# a sufficient workaround until we no longer need architecture dependent
# EXTRAVERSION...
MODLIB := $(INSTALL_MOD_PATH)/lib/modules/$(KERNELRELEASE)

ifeq ($(CONFIG_DEBUG_INFO),y)
CFLAGS := $(subst -fomit-frame-pointer,,$(CFLAGS))
endif

core-y			+= $(ARCH_DIR)/kernel/		 \
			   $(ARCH_DIR)/drivers/          \
			   $(ARCH_DIR)/sys-$(SUBARCH)/

# Have to precede the include because the included Makefiles reference them.
SYMLINK_HEADERS = include/asm-um/archparam.h include/asm-um/system.h \
	include/asm-um/sigcontext.h include/asm-um/processor.h \
	include/asm-um/ptrace.h include/asm-um/arch-signal.h

ARCH_SYMLINKS = include/asm-um/arch $(ARCH_DIR)/include/sysdep $(ARCH_DIR)/os \
	$(SYMLINK_HEADERS) $(ARCH_DIR)/include/uml-config.h

GEN_HEADERS += $(ARCH_DIR)/include/task.h $(ARCH_DIR)/include/kern_constants.h

include $(ARCH_DIR)/Makefile-$(SUBARCH)
include $(ARCH_DIR)/Makefile-os-$(OS)

MAKEFILE-$(CONFIG_MODE_TT) += Makefile-tt
MAKEFILE-$(CONFIG_MODE_SKAS) += Makefile-skas

ifneq ($(MAKEFILE-y),)
  include $(addprefix $(ARCH_DIR)/,$(MAKEFILE-y))
endif

EXTRAVERSION := $(EXTRAVERSION)-1um

ARCH_INCLUDE = -I$(ARCH_DIR)/include

# -Derrno=kernel_errno - This turns all kernel references to errno into
# kernel_errno to separate them from the libc errno.  This allows -fno-common
# in CFLAGS.  Otherwise, it would cause ld to complain about the two different
# errnos.

CFLAGS += $(CFLAGS-y) -D__arch_um__ -DSUBARCH=\"$(SUBARCH)\" \
	-D_LARGEFILE64_SOURCE $(ARCH_INCLUDE) -Derrno=kernel_errno \
	$(MODE_INCLUDE)

LINK_WRAPS = -Wl,--wrap,malloc -Wl,--wrap,free -Wl,--wrap,calloc

SIZE = (($(CONFIG_NEST_LEVEL) + $(CONFIG_KERNEL_HALF_GIGS)) * 0x20000000)

ifeq ($(CONFIG_MODE_SKAS), y)
$(SYS_HEADERS) : $(ARCH_DIR)/kernel/skas/include/skas_ptregs.h
endif

include/linux/version.h: arch/$(ARCH)/Makefile

$(ARCH_DIR)/vmlinux.lds.S :
	touch $@

prepare: $(ARCH_SYMLINKS) $(SYS_HEADERS) $(GEN_HEADERS)

LDFLAGS_vmlinux = -r

vmlinux: $(ARCH_DIR)/main.o 

# These aren't in Makefile-tt because they are needed in the !CONFIG_MODE_TT +
# CONFIG_MODE_SKAS + CONFIG_STATIC_LINK case.

LINK_TT = -static
LD_SCRIPT_TT := uml.lds.s

ifeq ($(CONFIG_STATIC_LINK),y)
  LINK-y += $(LINK_TT)
  LD_SCRIPT-y := $(LD_SCRIPT_TT)
else
ifeq ($(CONFIG_MODE_TT),y)
  LINK-y += $(LINK_TT)
  LD_SCRIPT-y := $(LD_SCRIPT_TT)
else
ifeq ($(CONFIG_MODE_SKAS),y)
  LINK-y += $(LINK_SKAS)
  LD_SCRIPT-y := $(LD_SCRIPT_SKAS)
endif
endif
endif

CPP_MODE_TT := $(shell [ "$(CONFIG_MODE_TT)" = "y" ] && echo -DMODE_TT)
CONFIG_KERNEL_STACK_ORDER ?= 2
STACK_SIZE := $(shell echo $$[ 4096 * (1 << $(CONFIG_KERNEL_STACK_ORDER)) ] )

AFLAGS_vmlinux.lds.o = -U$(SUBARCH) \
	-DSTART=$$(($(TOP_ADDR) - $(SIZE))) -DELF_ARCH=$(ELF_ARCH) \
	-DELF_FORMAT=\"$(ELF_FORMAT)\" $(CPP_MODE_TT) \
	-DKERNEL_STACK_SIZE=$(STACK_SIZE)

AFLAGS_$(LD_SCRIPT-y:.s=).o = $(AFLAGS_vmlinux.lds.o) -P -C -Uum

LD_SCRIPT-y := $(ARCH_DIR)/$(LD_SCRIPT-y)

$(LD_SCRIPT-y) : $(LD_SCRIPT-y:.s=.S) scripts FORCE
	$(call if_changed_dep,as_s_S)

linux: vmlinux $(LD_SCRIPT-y)
	$(CC) -Wl,-T,$(LD_SCRIPT-y) $(LINK-y) $(LINK_WRAPS) \
		-o linux $(ARCH_DIR)/main.o vmlinux -L/usr/lib -lutil

USER_CFLAGS := $(patsubst -I%,,$(CFLAGS))
USER_CFLAGS := $(patsubst -Derrno=kernel_errno,,$(USER_CFLAGS))
USER_CFLAGS := $(patsubst -D__KERNEL__,,$(USER_CFLAGS)) $(ARCH_INCLUDE) \
	$(MODE_INCLUDE)

# To get a definition of F_SETSIG
USER_CFLAGS += -D_GNU_SOURCE

CLEAN_FILES += linux x.i gmon.out $(ARCH_DIR)/uml.lds.s \
	$(ARCH_DIR)/dyn_link.ld.s $(GEN_HEADERS)

$(ARCH_DIR)/main.o: $(ARCH_DIR)/main.c
	$(CC) $(USER_CFLAGS) $(EXTRA_CFLAGS) -c -o $@ $<

archmrproper:
	for d in $(ARCH_SUBDIRS) $(ARCH_DIR)/util; \
	do \
		$(MAKE) -C $$d archmrproper; \
	done
	rm -f $(CLEAN_FILES) $(SYMLINK_HEADERS) $(ARCH_SYMLINKS) include/asm \
		$(addprefix $(ARCH_DIR)/kernel/,$(KERN_SYMLINKS))

archclean: sysclean
	for d in $(ARCH_SUBDIRS) $(ARCH_DIR)/util; \
	do \
		$(MAKE) -C $$d clean; \
	done
	find . \( -name '*.bb' -o -name '*.bbg' -o -name '*.da' \
		-o -name '*.gcov' \) -type f -print | xargs rm -f
	rm -f linux x.i gmon.out $(ARCH_DIR)/link.ld $(GEN_HEADERS)

archdep: 
	for d in $(ARCH_SUBDIRS); do $(MAKE) -C $$d fastdep; done

$(SYMLINK_HEADERS):
	cd $(TOPDIR)/$(dir $@) ; \
	ln -sf $(basename $(notdir $@))-$(SUBARCH)$(suffix $@) $(notdir $@)

include/asm-um/arch:
	cd $(TOPDIR)/include/asm-um && ln -sf ../asm-$(SUBARCH) arch

$(ARCH_DIR)/include/sysdep:
	cd $(ARCH_DIR)/include && ln -sf sysdep-$(SUBARCH) sysdep

$(ARCH_DIR)/os:
	cd $(ARCH_DIR) && ln -sf os-$(OS) os

$(ARCH_DIR)/include/uml-config.h :
	sed 's/ CONFIG/ UML_CONFIG/' $(TOPDIR)/include/linux/autoconf.h > $@

$(ARCH_DIR)/include/task.h : $(ARCH_DIR)/util/mk_task
	$< > $@

$(ARCH_DIR)/include/kern_constants.h : $(ARCH_DIR)/util/mk_constants
	$< > $@

$(ARCH_DIR)/util/mk_task : $(ARCH_DIR)/kernel/skas/include/skas_ptregs.h \
	$(ARCH_DIR)/util FORCE ;

$(ARCH_DIR)/util: FORCE
	@$(call descend,$@,)

export SUBARCH USER_CFLAGS OS
