#
# Makefile for the linux kernel.
#
# Note! Dependencies are done automagically by 'make dep', which also
# removes any old dependencies. DON'T put your own dependencies here
# unless it's something special (ie not a .c file).
#
# Note 2! The CFLAGS definitions are now in the main makefile...

all: hpux.o
O_TARGET = hpux.o
O_OBJS = entry_hpux.o gate.o wrappers.o fs.o ioctl.o sys_hpux.o

.o.S:	$(CC) -D__ASSEMBLY__ $(AFLAGS) -traditional -c $*.S -o $*.o

include $(TOPDIR)/Rules.make
