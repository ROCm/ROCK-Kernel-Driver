# 
# Copyright (C) 2002 Jeff Dike (jdike@karaya.com)
# Licensed under the GPL
#

ARCH_DIR = arch/um
OS := $(shell uname -s)
# We require bash because the vmlinux link and loader script cpp use bash
# features.
SHELL := /bin/bash

filechk_gen_header = $<

core-y			+= $(ARCH_DIR)/kernel/		\
			   $(ARCH_DIR)/drivers/		\
			   $(ARCH_DIR)/sys-$(SUBARCH)/

# Have to precede the include because the included Makefiles reference them.
SYMLINK_HEADERS = archparam.h system.h sigcontext.h processor.h ptrace.h \
	arch-signal.h module.h
SYMLINK_HEADERS := $(foreach header,$(SYMLINK_HEADERS),include/asm-um/$(header))

ARCH_SYMLINKS = include/asm-um/arch $(ARCH_DIR)/include/sysdep $(ARCH_DIR)/os \
	$(SYMLINK_HEADERS) $(ARCH_DIR)/include/uml-config.h

GEN_HEADERS += $(ARCH_DIR)/include/task.h $(ARCH_DIR)/include/kern_constants.h

MAKEFILE-$(CONFIG_MODE_TT) += Makefile-tt
MAKEFILE-$(CONFIG_MODE_SKAS) += Makefile-skas

ifneq ($(MAKEFILE-y),)
  include $(addprefix $(srctree)/$(ARCH_DIR)/,$(MAKEFILE-y))
endif

ARCH_INCLUDE	:= -I$(ARCH_DIR)/include
SYS_DIR		:= $(ARCH_DIR)/include/sysdep-$(SUBARCH)

include $(srctree)/$(ARCH_DIR)/Makefile-$(SUBARCH)
include $(srctree)/$(ARCH_DIR)/Makefile-os-$(OS)

# -Derrno=kernel_errno - This turns all kernel references to errno into
# kernel_errno to separate them from the libc errno.  This allows -fno-common
# in CFLAGS.  Otherwise, it would cause ld to complain about the two different
# errnos.

CFLAGS += $(CFLAGS-y) -D__arch_um__ -DSUBARCH=\"$(SUBARCH)\" \
	-D_LARGEFILE64_SOURCE $(ARCH_INCLUDE) -Derrno=kernel_errno \
	-Dsigprocmask=kernel_sigprocmask $(MODE_INCLUDE)

CFLAGS += $(call cc-option,-fno-unit-at-a-time,)

LINK_WRAPS = -Wl,--wrap,malloc -Wl,--wrap,free -Wl,--wrap,calloc

# These are needed for clean and mrproper, since in that case .config is not
# included; the values here are meaningless

CONFIG_NEST_LEVEL ?= 0
CONFIG_KERNEL_HALF_GIGS ?= 0

SIZE = (($(CONFIG_NEST_LEVEL) + $(CONFIG_KERNEL_HALF_GIGS)) * 0x20000000)

ifeq ($(CONFIG_MODE_SKAS), y)
$(SYS_HEADERS) : $(ARCH_DIR)/include/skas_ptregs.h
endif

.PHONY: linux

all: linux

linux: vmlinux
	ln -f $< $@

define archhelp
  echo '* linux		- Binary kernel image (./linux) - for backward'
  echo '		   compatibility only, this creates a hard link to the'
  echo '		   real kernel binary, the the "vmlinux" binary you'
  echo '		   find in the kernel root.'
endef

prepare: $(ARCH_SYMLINKS) $(SYS_HEADERS) $(GEN_HEADERS) \
	$(ARCH_DIR)/kernel/vmlinux.lds.S

# These aren't in Makefile-tt because they are needed in the !CONFIG_MODE_TT +
# CONFIG_MODE_SKAS + CONFIG_STATIC_LINK case.

LINK_TT = -static
LD_SCRIPT_TT := uml.lds.S

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

ifndef START
  START = $$(($(TOP_ADDR) - $(SIZE)))
endif

CPPFLAGS_vmlinux.lds = $(shell echo -U$(SUBARCH) \
	-DSTART=$(START) -DELF_ARCH=$(ELF_ARCH) \
	-DELF_FORMAT=\"$(ELF_FORMAT)\" $(CPP_MODE_TT) \
	-DKERNEL_STACK_SIZE=$(STACK_SIZE))

CFLAGS_vmlinux = $(LINK-y) $(LINK_WRAPS)
define cmd_vmlinux__
	$(CC) $(CFLAGS_vmlinux) -o $@ \
	-Wl,-T,$(vmlinux-lds) $(vmlinux-init) \
	-Wl,--start-group $(vmlinux-main) -Wl,--end-group \
	-L/usr/lib -lutil \
	$(filter-out $(vmlinux-lds) $(vmlinux-init) $(vmlinux-main) \
	FORCE ,$^) ; rm -f linux
endef

USER_CFLAGS := $(patsubst -I%,,$(CFLAGS))
USER_CFLAGS := $(patsubst -Derrno=kernel_errno,,$(USER_CFLAGS))
USER_CFLAGS := $(patsubst -Dsigprocmask=kernel_sigprocmask,,$(USER_CFLAGS))
USER_CFLAGS := $(patsubst -D__KERNEL__,,$(USER_CFLAGS)) $(ARCH_INCLUDE) \
	$(MODE_INCLUDE)
USER_CFLAGS += $(ARCH_USER_CFLAGS)

# To get a definition of F_SETSIG
USER_CFLAGS += -D_GNU_SOURCE

#When cleaning we don't include .config, so we don't include
#TT or skas makefiles and don't clean skas_ptregs.h.
CLEAN_FILES += linux x.i gmon.out $(ARCH_DIR)/include/uml-config.h \
	$(GEN_HEADERS) $(ARCH_DIR)/include/skas_ptregs.h

MRPROPER_FILES += $(SYMLINK_HEADERS) $(ARCH_SYMLINKS) \
	$(addprefix $(ARCH_DIR)/kernel/,$(KERN_SYMLINKS)) $(ARCH_DIR)/os

archmrproper:
	@:

archclean:
	$(Q)$(MAKE) $(clean)=$(ARCH_DIR)/util
	@find . \( -name '*.bb' -o -name '*.bbg' -o -name '*.da' \
		-o -name '*.gcov' \) -type f -print | xargs rm -f

#We need to re-preprocess this when the symlink dest changes.
#So we touch it.
$(ARCH_DIR)/kernel/vmlinux.lds.S: FORCE
	@echo '  SYMLINK $@'
	$(Q)ln -sf $(LD_SCRIPT-y) $@
	$(Q)touch $@

$(SYMLINK_HEADERS):
	@echo '  SYMLINK $@'
	$(Q)cd $(TOPDIR)/$(dir $@) ; \
	ln -sf $(basename $(notdir $@))-$(SUBARCH)$(suffix $@) $(notdir $@)

include/asm-um/arch:
	@echo '  SYMLINK $@'
	$(Q)cd $(TOPDIR)/include/asm-um && ln -sf ../asm-$(SUBARCH) arch

$(ARCH_DIR)/include/sysdep:
	@echo '  SYMLINK $@'
	$(Q)cd $(ARCH_DIR)/include && ln -sf sysdep-$(SUBARCH) sysdep

$(ARCH_DIR)/os:
	@echo '  SYMLINK $@'
	$(Q)cd $(ARCH_DIR) && ln -sf os-$(OS) os

# Generated files
define filechk_umlconfig
	sed 's/ CONFIG/ UML_CONFIG/'
endef

$(ARCH_DIR)/include/uml-config.h : include/linux/autoconf.h
	$(call filechk,umlconfig)

$(ARCH_DIR)/include/task.h: $(ARCH_DIR)/util/mk_task
	$(call filechk,gen_header)

$(ARCH_DIR)/include/kern_constants.h: $(ARCH_DIR)/util/mk_constants
	$(call filechk,gen_header)

$(ARCH_DIR)/include/skas_ptregs.h: $(ARCH_DIR)/kernel/skas/util/mk_ptregs
	$(call filechk,gen_header)

$(ARCH_DIR)/util/mk_task $(ARCH_DIR)/util/mk_constants: $(ARCH_DIR)/util \
	FORCE ;

$(ARCH_DIR)/kernel/skas/util/mk_ptregs: $(ARCH_DIR)/kernel/skas/util FORCE ;

$(ARCH_DIR)/util: scripts_basic $(SYS_DIR)/sc.h FORCE
	$(Q)$(MAKE) $(build)=$@

$(ARCH_DIR)/kernel/skas/util: scripts_basic FORCE
	$(Q)$(MAKE) $(build)=$@

export SUBARCH USER_CFLAGS OS
