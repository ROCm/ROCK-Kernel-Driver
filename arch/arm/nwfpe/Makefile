#
# linux/arch/arm/nwfpe/Makefile
#
# Copyright (C) 1998, 1999 Philip Blundell
#

USE_STANDARD_AS_RULE := true

NWFPE_OBJS := fpa11.o fpa11_cpdo.o fpa11_cpdt.o fpa11_cprt.o \
	      fpmodule.o fpopcode.o softfloat.o \
	      single_cpdo.o double_cpdo.o extended_cpdo.o

ifeq ($(CONFIG_CPU_26),y)
NWFPE_OBJS += entry26.o
else
NWFPE_OBJS += entry.o
endif

ifeq ($(CONFIG_NWFPE),y)
O_TARGET := math-emu.o
O_OBJS = $(NWFPE_OBJS)
else
  ifeq ($(CONFIG_NWFPE),m)
    M_OBJS = nwfpe.o
    MI_OBJS = $(NWFPE_OBJS)
  endif
endif

include $(TOPDIR)/Rules.make

nwfpe.o: $(MI_OBJS) $(MIX_OBJS)
	 $(LD) $(LD_RFLAG) -r -o $@ $(MI_OBJS) $(MIX_OBJS)
