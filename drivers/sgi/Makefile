#
# Makefile for the linux kernel.
#
# Note! Dependencies are done automagically by 'make dep', which also
# removes any old dependencies. DON'T put your own dependencies here
# unless it's something special (ie not a .c file).
#
# Note 2! The CFLAGS definitions are now in the main makefile...

SUB_DIRS     :=
MOD_SUB_DIRS := $(SUB_DIRS) char
ALL_SUB_DIRS := $(SUB_DIRS) char


L_OBJS   := 
L_TARGET := sgi.a

# Character and Audio devices for SGI machines.
#
SUB_DIRS += char
L_OBJS += char/sgichar.o

include $(TOPDIR)/Rules.make
