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
 
oprofile-objs := $(DRIVER_OBJS) init.o

oprofile-$(CONFIG_X86_LOCAL_APIC) += nmi_int.o op_model_athlon.o
 
INCL := $(obj)/op_counter.h $(obj)/op_x86_model.h

$(obj)/nmi_int.c: ${INCL}
	@ln -sf ../../i386/oprofile/nmi_int.c $(obj)/nmi_int.c
$(obj)/op_model_athlon.c: ${INCL}
	@ln -sf ../../i386/oprofile/op_model_athlon.c $(obj)/op_model_athlon.c
$(obj)/init.c: ${INCL}
	@ln -sf ../../i386/oprofile/init.c $(obj)/init.c
$(obj)/op_counter.h: 
	@ln -sf ../../i386/oprofile/op_counter.h $(obj)/op_counter.h
$(obj)/op_x86_model.h:
	@ln -sf ../../i386/oprofile/op_x86_model.h $(obj)/op_x86_model.h	
clean-files += op_x86_model.h op_counter.h init.c \
	       op_model_athlon.c nmi_int.c
