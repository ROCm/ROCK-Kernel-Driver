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
SYMLINK_HEADERS = archparam.h system.h sigcontext.h processor.h ptrace.h \
	arch-signal.h module.h
SYMLINK_HEADERS := $(foreach header,$(SYMLINK_HEADERS),include/asm-um/$(header))

ARCH_SYMLINKS = include/asm-um/arch $(ARCH_DIR)/include/sysdep $(ARCH_DIR)/os \
	$(SYMLINK_HEADERS) $(ARCH_DIR)/include/uml-config.h

GEN_HEADERS += $(ARCH_DIR)/include/task.h $(ARCH_DIR)/include/kern_constants.h

# This target adds dependencies to "prepare". They are defined in the included
# Makefiles (see Makefile-i386).

.PHONY: sys_prepare
sys_prepare:
	@:

MAKEFILE-$(CONFIG_MODE_TT) += Makefile-tt
MAKEFILE-$(CONFIG_MODE_SKAS) += Makefile-skas

ifneq ($(MAKEFILE-y),)
  include $(addprefix $(ARCH_DIR)/,$(MAKEFILE-y))
endif

include $(ARCH_DIR)/Makefile-$(SUBARCH)
include $(ARCH_DIR)/Makefile-os-$(OS)

EXTRAVERSION := $(EXTRAVERSION)-1um

ARCH_INCLUDE = -I$(ARCH_DIR)/include

# -Derrno=kernel_errno - This turns all kernel references to errno into
# kernel_errno to separate them from the libc errno.  This allows -fno-common
# in CFLAGS.  Otherwise, it would cause ld to complain about the two different
# errnos.

CFLAGS += $(CFLAGS-y) -D__arch_um__ -DSUBARCH=\"$(SUBARCH)\" \
	-D_LARGEFILE64_SOURCE $(ARCH_INCLUDE) -Derrno=kernel_errno \
	-Dsigprocmask=kernel_sigprocmask $(MODE_INCLUDE)

CFLAGS += $(call check_gcc,-fno-unit-at-a-time,)

LINK_WRAPS = -Wl,--wrap,malloc -Wl,--wrap,free -Wl,--wrap,calloc

# These are needed for clean and mrproper, since in that case .config is not
# included; the values here are meaningless

CONFIG_NEST_LEVEL ?= 0
CONFIG_KERNEL_HALF_GIGS ?=  0

SIZE = (($(CONFIG_NEST_LEVEL) + $(CONFIG_KERNEL_HALF_GIGS)) * 0x20000000)

ifeq ($(CONFIG_MODE_SKAS), y)
$(SYS_HEADERS) : $(TOPDIR)/$(ARCH_DIR)/include/skas_ptregs.h
endif

include/linux/version.h: arch/$(ARCH)/Makefile

$(ARCH_DIR)/vmlinux.lds.S :
	touch $@

prepare: $(ARCH_SYMLINKS) $(SYS_HEADERS) $(GEN_HEADERS)

LDFLAGS_vmlinux = -r

# These aren't in Makefile-tt because they are needed in the !CONFIG_MODE_TT +
# CONFIG_MODE_SKAS + CONFIG_STATIC_LINK case.

LINK_TT = -static
LD_SCRIPT_TT := uml.lds

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

CPPFLAGS_vmlinux.lds = $(shell echo -U$(SUBARCH) \
	-DSTART=$$(($(TOP_ADDR) - $(SIZE))) -DELF_ARCH=$(ELF_ARCH) \
	-DELF_FORMAT=\"$(ELF_FORMAT)\" $(CPP_MODE_TT) \
	-DKERNEL_STACK_SIZE=$(STACK_SIZE))

export CPPFLAGS_$(LD_SCRIPT-y) = $(CPPFLAGS_vmlinux.lds) -P -C -Uum

LD_SCRIPT-y := $(ARCH_DIR)/kernel/$(LD_SCRIPT-y)

linux: vmlinux $(LD_SCRIPT-y)
	$(CC) -Wl,-T,$(LD_SCRIPT-y) $(LINK-y) $(LINK_WRAPS) \
		-o linux vmlinux -L/usr/lib -lutil

USER_CFLAGS := $(patsubst -I%,,$(CFLAGS))
USER_CFLAGS := $(patsubst -Derrno=kernel_errno,,$(USER_CFLAGS))
USER_CFLAGS := $(patsubst -Dsigprocmask=kernel_sigprocmask,,$(USER_CFLAGS))
USER_CFLAGS := $(patsubst -D__KERNEL__,,$(USER_CFLAGS)) $(ARCH_INCLUDE) \
	$(MODE_INCLUDE)

# To get a definition of F_SETSIG
USER_CFLAGS += -D_GNU_SOURCE

# From main Makefile, these options are set after including the ARCH makefile.
# So copy them here.

ifdef CONFIG_CC_OPTIMIZE_FOR_SIZE
USER_CFLAGS		+= -Os
else
USER_CFLAGS		+= -O2
endif

ifndef CONFIG_FRAME_POINTER
USER_CFLAGS		+= -fomit-frame-pointer
endif

ifdef CONFIG_DEBUG_INFO
USER_CFLAGS		+= -g
endif

CLEAN_FILES += linux x.i gmon.out $(ARCH_DIR)/uml.lds \
	$(ARCH_DIR)/dyn_link.ld.s $(ARCH_DIR)/include/uml-config.h \
	$(GEN_HEADERS)

MRPROPER_FILES += $(SYMLINK_HEADERS) $(ARCH_SYMLINKS) \
	$(addprefix $(ARCH_DIR)/kernel/,$(KERN_SYMLINKS))

archmrproper:
	@:

archclean:
	@find . \( -name '*.bb' -o -name '*.bbg' -o -name '*.da' \
		-o -name '*.gcov' \) -type f -print | xargs rm -f

$(SYMLINK_HEADERS):
	cd $(TOPDIR)/$(dir $@) ; \
	ln -sf $(basename $(notdir $@))-$(SUBARCH)$(suffix $@) $(notdir $@)

include/asm-um/arch:
	cd $(TOPDIR)/include/asm-um && ln -sf ../asm-$(SUBARCH) arch

$(ARCH_DIR)/include/sysdep:
	cd $(ARCH_DIR)/include && ln -sf sysdep-$(SUBARCH) sysdep

$(ARCH_DIR)/os:
	cd $(ARCH_DIR) && ln -sf os-$(OS) os

# Generated files
define filechk_umlconfig
	sed 's/ CONFIG/ UML_CONFIG/'
endef

$(ARCH_DIR)/include/uml-config.h : $(TOPDIR)/include/linux/autoconf.h
	$(call filechk,umlconfig)

filechk_gen_header = $<

$(ARCH_DIR)/include/task.h : $(ARCH_DIR)/util/mk_task
	$(call filechk,gen_header)

$(ARCH_DIR)/include/kern_constants.h : $(ARCH_DIR)/util/mk_constants
	$(call filechk,gen_header)

$(ARCH_DIR)/util/mk_task $(ARCH_DIR)/util/mk_constants : $(ARCH_DIR)/util \
	sys_prepare FORCE ;

$(ARCH_DIR)/util: FORCE
	$(Q)$(MAKE) $(build)=$@

export SUBARCH USER_CFLAGS OS

all: linux

define archhelp
  echo  '* linux	- Binary kernel image (./linux)'
endef
