
EXTRA_TARGETS	:= unmap_fin.o

obj-y = config.o exec_kern.o exec_user.o exitcode.o frame_kern.o frame.o \
	helper.o init_task.o irq.o irq_user.o ksyms.o mem.o mem_user.o \
	process.o process_kern.o ptrace.o reboot.o resource.o setup.o \
	sigio_user.o sigio_kern.o signal_kern.o signal_user.o smp.o \
	syscall_kern.o syscall_user.o sysrq.o sys_call_table.o time.o \
	time_kern.o tlb.o trap_kern.o trap_user.o uaccess_user.o um_arch.o \
	umid.o user_util.o

obj-$(CONFIG_BLK_DEV_INITRD) += initrd_kern.o initrd_user.o

# user_syms.o not included here because Rules.make has its own ideas about
# building anything in export-objs

USER_OBJS = $(filter %_user.o,$(obj-y)) config.o process.o time.o umid.o \
	user_util.o user_syms.o helper.o tty_log.o

export-objs := ksyms.o process_kern.o signal_kern.o gprof_syms.o gmon_syms.o

UNMAP_CFLAGS := $(patsubst -pg -DPROFILING,,$(USER_CFLAGS))
UNMAP_CFLAGS := $(patsubst -fprofile-arcs -ftest-coverage,,$(UNMAP_CFLAGS))

ifeq ($(CONFIG_MODULES), y)
  DMODULES = -D__CONFIG_MODULES__
endif

ifeq ($(CONFIG_MODVERSIONS), y)
  DMODVERSIONS = -D__CONFIG_MODVERSIONS__
endif

obj-$(CONFIG_GPROF)	+= gprof_syms.o
obj-$(CONFIG_GCOV)	+= gmon_syms.o
obj-$(CONFIG_TTY_LOG)	+= tty_log.o

CFLAGS_user_syms.o = -D__AUTOCONF_INCLUDED__ $(DMODULES) $(DMODVERSIONS) \
	-I/usr/include -I../include

CFLAGS_frame.o := $(patsubst -fomit-frame-pointer,,$(USER_CFLAGS))

include $(TOPDIR)/Rules.make

$(USER_OBJS) : %.o: %.c
	$(CC) $(CFLAGS_$@) $(USER_CFLAGS) -c -o $@ $<

unmap.o: unmap.c
	$(CC) $(UNMAP_CFLAGS) -c -o $@ $<

frame.o: frame.c
	$(CC) $(CFLAGS_$@) -c -o $@ $<

unmap_fin.o : unmap.o
	ld -r -o $@ $< -lc -L/usr/lib

QUOTE = 'my $$config=`cat $(TOPDIR)/.config`; $$config =~ s/"/\\"/g ; while(<STDIN>) { $$_ =~ s/CONFIG/$$config/; print $$_ }'

config.c : config.c.in $(TOPDIR)/.config
	$(PERL) -e $(QUOTE) < config.c.in > $@

clean:
	rm -f config.c

modules:

fastdep:

dep:

archmrproper: clean

