#
# Makefile for the linux kernel.
#
# Note! Dependencies are done automagically by 'make dep', which also
# removes any old dependencies. DON'T put your own dependencies here
# unless it's something special (ie not a .c file).
#
# Note 2! The CFLAGS definitions are now in the main makefile...

SUB_DIRS     :=
MOD_SUB_DIRS := 
ALL_SUB_DIRS := 

L_TARGET := tc.a
L_OBJS   := tc.o

# Nasty trick as nobody references tcsyms.o, but we still want it linked.
# Stolen from pci Makefile
ifeq ($(CONFIG_MODULES),y)
O_TARGET = tc_syms.o
OX_OBJS  = tcsyms.o
O_OBJS   = tc.o
L_OBJS   := tc_syms.o
else
L_OBJS   := tc.o
endif

ifdef CONFIG_ZS
L_OBJS   += zs.o
endif

include $(TOPDIR)/Rules.make

