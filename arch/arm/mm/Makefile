#
# Makefile for the linux arm-specific parts of the memory manager.
#
# Note! Dependencies are done automagically by 'make dep', which also
# removes any old dependencies. DON'T put your own dependencies here
# unless it's something special (ie not a .c file).
#
# Note 2! The CFLAGS definition is now in the main makefile...

USE_STANDARD_AS_RULE := true

O_TARGET	:= mm.o

# Object file lists.

obj-y		:= extable.o fault-common.o fault-$(PROCESSOR).o init.o \
		   mm-$(PROCESSOR).o small_page.o
obj-m		:=
obj-n		:=
obj-		:=
export-objs	:= proc-syms.o

p-$(CONFIG_CPU_26)	+= proc-arm2,3.o
p-$(CONFIG_CPU_ARM610)	+= proc-arm6,7.o
p-$(CONFIG_CPU_ARM710)	+= proc-arm6,7.o
p-$(CONFIG_CPU_ARM720T)	+= proc-arm720.o
p-$(CONFIG_CPU_ARM920T)	+= proc-arm920.o
p-$(CONFIG_CPU_ARM10)	+= proc-arm10.o
p-$(CONFIG_CPU_SA110)	+= proc-sa110.o
p-$(CONFIG_CPU_SA1100)	+= proc-sa110.o

obj-$(CONFIG_CPU_32)  	+= consistent.o ioremap.o
ifeq ($(CONFIG_CPU_32),y)
obj-$(CONFIG_MODULES)	+= proc-syms.o
endif

# Integrator follows "new style"
# Soon, others will do too, and we can get rid of this
MMMACH		:= mm-$(MACHINE).c
ifeq ($(MMMACH),$(wildcard $(MMMACH)))
obj-$(CONFIG_CPU_32)	+= $(MMMACH:.c=.o)
endif

obj-y		+= $(sort $(p-y))

include $(TOPDIR)/Rules.make

# Special dependencies
proc-arm2,3.o:	../lib/constants.h
proc-arm6,7.o:	../lib/constants.h
proc-arm720.o:	../lib/constants.h
proc-arm920.o:	../lib/constants.h
proc-arm10.o:	../lib/constants.h
proc-sa110.o:	../lib/constants.h

