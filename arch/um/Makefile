ARCH_DIR = arch/um
OS := $(shell uname -s)

include $(ARCH_DIR)/Makefile-$(SUBARCH)
include $(ARCH_DIR)/Makefile-os-$(OS)

EXTRAVERSION := $(EXTRAVERSION)-1um
include/linux/version.h: arch/$(ARCH)/Makefile

# Recalculate MODLIB to reflect the EXTRAVERSION changes (via KERNELRELEASE)
# The way the toplevel Makefile is written EXTRAVERSION is not supposed
# to be changed outside the toplevel Makefile, but recalculating MODLIB is
# a sufficient workaround until we no longer need architecture dependent
# EXTRAVERSION...
MODLIB := $(INSTALL_MOD_PATH)/lib/modules/$(KERNELRELEASE)

MAKEBOOT = $(MAKE) -C $(ARCH_DIR)/boot

ifeq ($(CONFIG_DEBUGSYM),y)
DEBUG = -g
CFLAGS := $(subst -fomit-frame-pointer,,$(CFLAGS))
endif

ifeq ($(CONFIG_GCOV),y)
CFLAGS += -fprofile-arcs -ftest-coverage
endif

ifeq ($(CONFIG_GPROF), y)
PROFILE += -pg -DPROFILING
LINK_PROFILE = $(PROFILE) -Wl,--wrap,__monstartup
endif

ARCH_SUBDIRS = $(ARCH_DIR)/drivers $(ARCH_DIR)/kernel \
	$(ARCH_DIR)/sys-$(SUBARCH) $(ARCH_DIR)/os-$(OS)

SUBDIRS += $(ARCH_SUBDIRS)

LIBS += $(ARCH_DIR)/kernel/um.o $(ARCH_DIR)/drivers/drivers.o \
	$(ARCH_DIR)/sys-$(SUBARCH)/sys.o $(ARCH_DIR)/os/os.o -L/usr/lib -lutil

ifeq ($(CONFIG_PT_PROXY), y)
SUBDIRS += $(ARCH_DIR)/ptproxy
LIBS += $(ARCH_DIR)/ptproxy/ptproxy.a
endif

ARCH_INCLUDE = $(TOPDIR)/$(ARCH_DIR)/include

# -Derrno=kernel_errno - This turns all kernel references to errno into
# kernel_errno to separate them from the libc errno.  This allows -fno-common
# in CFLAGS.  Otherwise, it would cause ld to complain about the two different
# errnos.

CFLAGS += $(DEBUG) $(PROFILE) $(ARCH_CFLAGS) -D__arch_um__ \
	-DSUBARCH=\"$(SUBARCH)\" -D_LARGEFILE64_SOURCE -I$(ARCH_INCLUDE) \
	-Derrno=kernel_errno

LDFLAGS += -r

LINK_WRAPS = -Wl,--wrap,malloc -Wl,--wrap,free -Wl,--wrap,calloc

SIZE = (($(CONFIG_NEST_LEVEL) + $(CONFIG_KERNEL_HALF_GIGS)) * 0x20000000)

AFLAGS_vmlinux.lds.o = -U$(SUBARCH) -DSTART=$$(($(TOP_ADDR) - $(SIZE))) \
	-DELF_ARCH=$(ELF_ARCH) -DELF_FORMAT=\"$(ELF_FORMAT)\"

LD_vmlinux = $(CC) 
LDFLAGS_vmlinux = $(LINK_PROFILE) $(LINK_WRAPS) -static $(ARCH_DIR)/main.o 

cmd_link_vmlinux := $(CC) $(LINK_PROFILE) $(LINK_WRAPS) -static \
	$(subst $(cmd_link_vmlinux),$(LD),) $(ARCH_DIR)/main.o vmlinux.o \
	-L/usr/lib -lutil

SYMLINK_HEADERS = include/asm-um/archparam.h include/asm-um/system.h \
	include/asm-um/sigcontext.h include/asm-um/processor.h \
	include/asm-um/ptrace.h include/asm-um/arch-signal.h

ARCH_SYMLINKS = include/asm-um/arch arch/um/include/sysdep arch/um/os \
	$(SYMLINK_HEADERS)

GEN_HEADERS = $(ARCH_DIR)/include/task.h

linux: $(ARCH_SYMLINKS) $(GEN_HEADERS) $(SYS_HEADERS) $(ARCH_DIR)/main.o \
	vmlinux
	mv vmlinux linux

USER_CFLAGS := $(patsubst -I%,,$(CFLAGS))
USER_CFLAGS := $(patsubst -Derrno=kernel_errno,,$(USER_CFLAGS))
USER_CFLAGS := $(patsubst -D__KERNEL__,,$(USER_CFLAGS)) -I$(ARCH_INCLUDE)

# To get a definition of F_SETSIG
USER_CFLAGS += -D_GNU_SOURCE

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
	@$(MAKEBOOT) clean

archdep: 
	for d in $(ARCH_SUBDIRS); do $(MAKE) -C $$d fastdep; done
	@$(MAKEBOOT) dep

$(SYMLINK_HEADERS):
	cd $(TOPDIR)/$(dir $@) ; \
	ln -sf $(basename $(notdir $@))-$(SUBARCH)$(suffix $@) $(notdir $@)

include/asm-um/arch:
	cd $(TOPDIR)/include/asm-um && ln -sf ../asm-$(SUBARCH) arch

arch/um/include/sysdep:
	cd $(TOPDIR)/arch/um/include && ln -sf sysdep-$(SUBARCH) sysdep

arch/um/os:
	cd $(ARCH_DIR) && ln -sf os-$(OS) os

$(ARCH_DIR)/include/task.h : $(ARCH_DIR)/util/mk_task
	$< > $@

$(ARCH_DIR)/util/mk_task : $(SYS_HEADERS) $(ARCH_DIR)/util/mk_task_user.c \
	$(ARCH_DIR)/util/mk_task_kern.c 
	$(MAKE) $(MFLAGS) -C $(ARCH_DIR)/util all

export SUBARCH USER_CFLAGS OS
