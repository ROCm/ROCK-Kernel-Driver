vpath %.c = . $(TOPDIR)/drivers/oprofile

obj-$(CONFIG_OPROFILE) += oprofile.o
 
DRIVER_OBJS = $(addprefix ../../../drivers/oprofile/, \
	oprof.o cpu_buffer.o buffer_sync.o \
	event_buffer.o oprofile_files.o \
	oprofilefs.o oprofile_stats.o )
 
oprofile-objs := $(DRIVER_OBJS) init.o timer_int.o

ifdef CONFIG_X86_LOCAL_APIC
oprofile-objs += nmi_int.o op_model_athlon.o op_model_ppro.o
endif
 
include $(TOPDIR)/Rules.make
