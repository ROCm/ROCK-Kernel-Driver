#
# Makefile for the Linux/SuperH kernel.
#
# Note! Dependencies are done automagically by 'make dep', which also
# removes any old dependencies. DON'T put your own dependencies here
# unless it's something special (ie not a .c file).
#

all: kernel.o head.o init_task.o

clean:

O_TARGET := kernel.o

export-objs	:= io.o io_generic.o io_hd64461.o setup_hd64461.o sh_ksyms.o

obj-y	:= process.o signal.o entry.o traps.o irq.o irq_ipr.o \
	ptrace.o setup.o time.o sys_sh.o semaphore.o \
	irq_imask.o io.o io_generic.o sh_ksyms.o

obj-$(CONFIG_CF_ENABLER)	+= cf-enabler.o
obj-$(CONFIG_CPU_SH4)		+= fpu.o
obj-$(CONFIG_PCI)		+= pci-sh.o 
obj-$(CONFIG_SH_RTC)            += rtc.o
obj-$(CONFIG_SH_STANDARD_BIOS)	+= sh_bios.o

obj-$(CONFIG_SH_HP600)		+= mach_hp600.o
machine-specific-objs		+= mach_hp600.o

obj-$(CONFIG_SH_SOLUTION_ENGINE)+= mach_se.o setup_se.o io_se.o led_se.o
machine-specific-objs		+= mach_se.o setup_se.o io_se.o led_se.o

obj-$(CONFIG_SH_CQREEK)		+= setup_cqreek.o
machine-specific-objs		+= setup_cqreek.o

obj-$(CONFIG_SH_UNKNOWN)	+= mach_unknown.o io_unknown.o
machine-specific-objs		+= mach_unknown.o io_unknown.o

obj-$(CONFIG_HD64461)		+= setup_hd64461.o io_hd64461.o
machine-specific-objs		+= setup_hd64461.o io_hd64461.o

# Doesn't compile well, so don't include in machine-specific-objs
obj-$(CONFIG_HD64465)		+= setup_hd64465.o io_hd64465.o
obj-$(CONFIG_SH_FOOBAR)		+= mach_foobar.o

ifeq ($(CONFIG_SH_GENERIC),y)
obj-y		+= $(machine-specific-objs)
endif

USE_STANDARD_AS_RULE := true

include $(TOPDIR)/Rules.make
