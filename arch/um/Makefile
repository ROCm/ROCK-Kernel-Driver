# 
# Copyright (C) 2002 Jeff Dike (jdike@karaya.com)
# Licensed under the GPL
#

ARCH_DIR = arch/um
OS := $(shell uname -s)

EXTRAVERSION := $(EXTRAVERSION)-1um
include/linux/version.h: arch/$(ARCH)/Makefile

# Recalculate MODLIB to reflect the EXTRAVERSION changes (via KERNELRELEASE)
# The way the toplevel Makefile is written EXTRAVERSION is not supposed
# to be changed outside the toplevel Makefile, but recalculating MODLIB is
# a sufficient workaround until we no longer need architecture dependent
# EXTRAVERSION...
MODLIB := $(INSTALL_MOD_PATH)/lib/modules/$(KERNELRELEASE)

ifeq ($(CONFIG_DEBUGSYM),y)
CFLAGS := $(subst -fomit-frame-pointer,,$(CFLAGS))
endif

CFLAGS-$(CONFIG_DEBUGSYM) += -g
CFLAGS-$(CONFIG_GCOV) += -fprofile-arcs -ftest-coverage
CFLAGS-$(CONFIG_GPROF) += $(PROFILE)
LINK-$(CONFIG_GPROF) += $(PROFILE) -Wl,--wrap,__monstartup

core-y			+= $(ARCH_DIR)/kernel/		 \
			   $(ARCH_DIR)/drivers/          \
			   $(ARCH_DIR)/sys-$(SUBARCH)/

ARCH_INCLUDE = -I$(ARCH_DIR)/include
MODE_INCLUDE = -I$(ARCH_DIR)/kernel/tt/include \
	-I$(ARCH_DIR)/kernel/skas/include

# -Derrno=kernel_errno - This turns all kernel references to errno into
# kernel_errno to separate them from the libc errno.  This allows -fno-common
# in CFLAGS.  Otherwise, it would cause ld to complain about the two different
# errnos.

CFLAGS += $(CFLAGS-y) -D__arch_um__ -DSUBARCH=\"$(SUBARCH)\" \
	-D_LARGEFILE64_SOURCE $(ARCH_INCLUDE) -Derrno=kernel_errno \
	$(MODE_INCLUDE)

LINK_WRAPS = -Wl,--wrap,malloc -Wl,--wrap,free -Wl,--wrap,calloc

SIZE = (($(CONFIG_NEST_LEVEL) + $(CONFIG_KERNEL_HALF_GIGS)) * 0x20000000)

SYMLINK_HEADERS = include/asm-um/archparam.h include/asm-um/system.h \
	include/asm-um/sigcontext.h include/asm-um/processor.h \
	include/asm-um/ptrace.h include/asm-um/arch-signal.h

ARCH_SYMLINKS = include/asm-um/arch $(ARCH_DIR)/include/sysdep $(ARCH_DIR)/os \
	$(SYMLINK_HEADERS) $(ARCH_DIR)/include/uml-config.h

ifeq ($(CONFIG_MODE_SKAS), y)
GEN_HEADERS = $(ARCH_DIR)/kernel/skas/include/skas_ptregs.h

$(SYS_HEADERS) : $(ARCH_DIR)/kernel/skas/include/skas_ptregs.h
endif

GEN_HEADERS += $(ARCH_DIR)/include/task.h $(ARCH_DIR)/include/kern_constants.h

include $(ARCH_DIR)/Makefile-$(SUBARCH)
include $(ARCH_DIR)/Makefile-os-$(OS)

$(ARCH_DIR)/vmlinux.lds.S :
	touch $@

prepare: $(ARCH_SYMLINKS) $(SYS_HEADERS) $(GEN_HEADERS)

LDFLAGS_vmlinux = -r

vmlinux: $(ARCH_DIR)/main.o 

$(ARCH_DIR)/uml.lds.s : $(ARCH_DIR)/uml.lds.S scripts FORCE
	$(call if_changed_dep,as_s_S)

AFLAGS_uml.lds.o = -U$(SUBARCH) -DSTART=$$(($(TOP_ADDR) - $(SIZE))) \
	-DELF_ARCH=$(ELF_ARCH) -DELF_FORMAT=\"$(ELF_FORMAT)\" -P -C -Uum

linux: $(ARCH_DIR)/uml.lds.s vmlinux
	$(CC) -Wl,-T,$(ARCH_DIR)/uml.lds.s -static $(LINK-y) $(LINK_WRAPS) \
		-o linux $(ARCH_DIR)/main.o vmlinux -L/usr/lib -lutil


USER_CFLAGS := $(patsubst -I%,,$(CFLAGS))
USER_CFLAGS := $(patsubst -Derrno=kernel_errno,,$(USER_CFLAGS))
USER_CFLAGS := $(patsubst -D__KERNEL__,,$(USER_CFLAGS)) $(ARCH_INCLUDE) \
	$(MODE_INCLUDE)

# To get a definition of F_SETSIG
USER_CFLAGS += -D_GNU_SOURCE

CLEAN_FILES += linux x.i gmon.out $(ARCH_DIR)/link.ld $(GEN_HEADERS)

$(ARCH_DIR)/main.o: $(ARCH_DIR)/main.c
	$(CC) $(USER_CFLAGS) $(EXTRA_CFLAGS) -c -o $@ $<

archmrproper:
	for d in $(ARCH_SUBDIRS) $(ARCH_DIR)/util; \
	do \
		$(MAKE) -C $$d archmrproper; \
	done
	rm -f $(SYMLINK_HEADERS) $(ARCH_SYMLINKS) include/asm \
		$(ARCH_DIR)/link.ld \
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
	ln -sf $(TOPDIR)/include/linux/autoconf.h $@

$(ARCH_DIR)/include/task.h : $(ARCH_DIR)/util/mk_task
	$< > $@

$(ARCH_DIR)/include/kern_constants.h : $(ARCH_DIR)/util/mk_constants
	$< > $@

$(ARCH_DIR)/util/mk_task : $(ARCH_DIR)/kernel/skas/include/skas_ptregs.h \
	$(ARCH_DIR)/util FORCE ;

$(ARCH_DIR)/util: FORCE
	@$(call descend,$@,)

$(ARCH_DIR)/kernel/skas/include/skas_ptregs.h :
	$(MAKE) -C $(ARCH_DIR)/kernel/skas include/skas_ptregs.h

export SUBARCH USER_CFLAGS OS
