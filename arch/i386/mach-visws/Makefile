#
# Makefile for the linux kernel.
#
# Note! Dependencies are done automagically by 'make dep', which also
# removes any old dependencies. DON'T put your own dependencies here
# unless it's something special (ie not a .c file).
#
# Note 2! The CFLAGS definitions are now in the main makefile...

.S.o:
	$(CC) $(AFLAGS) -traditional -c $< -o $*.o

all: mach-visws.o

O_TARGET 	:= mach-visws.o
EXTRA_CFLAGS	+= -I../kernel
export-objs     := 

obj-y				:= setup.o traps.o

obj-$(CONFIG_PCI)		+= pci-visws.o
obj-$(CONFIG_X86_VISWS_APIC)	+= visws_apic.o
obj-$(CONFIG_X86_LOCAL_APIC)	+= mpparse.o

include $(TOPDIR)/Rules.make
