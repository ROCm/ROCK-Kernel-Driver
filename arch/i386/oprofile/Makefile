obj-$(CONFIG_OPROFILE) += oprofile.o
 
DRIVER_OBJS = $(addprefix ../../../drivers/oprofile/, \
	oprof.o cpu_buffer.o buffer_sync.o \
	event_buffer.o oprofile_files.o \
	oprofilefs.o oprofile_stats.o )
 
oprofile-objs := $(DRIVER_OBJS) init.o timer_int.o

oprofile-$(CONFIG_X86_LOCAL_APIC) += nmi_int.o op_model_athlon.o op_model_ppro.o
 
include $(TOPDIR)/Rules.make
