#
# oprofile for x86-64.
# Just reuse the one from i386. The Hammer performance counters 
# are similar to Athlon.
#

obj-$(CONFIG_OPROFILE) += oprofile.o
 
DRIVER_OBJS = $(addprefix ../../../drivers/oprofile/, \
	oprof.o cpu_buffer.o buffer_sync.o \
	event_buffer.o oprofile_files.o \
	oprofilefs.o oprofile_stats.o \
	timer_int.o )

OPROFILE-y := init.o
OPROFILE-$(CONFIG_X86_LOCAL_APIC) += nmi_int.o op_model_athlon.o
OPROFILE-$(CONFIG_X86_IO_APIC)    += nmi_timer_int.o 

oprofile-y = $(DRIVER_OBJS) $(addprefix ../../i386/oprofile/, $(OPROFILE-y))
