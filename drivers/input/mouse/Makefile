#
# Makefile for the mouse drivers.
#

# The target object and module list name.

O_TARGET	:= mousedrv.o

# Each configuration option enables a list of files.

obj-$(CONFIG_MOUSE_AMIGA)	+= amimouse.o
obj-$(CONFIG_MOUSE_ACORN)	+= rpcmouse.o
obj-$(CONFIG_MOUSE_INPORT)	+= inport.o
obj-$(CONFIG_MOUSE_LOGIBM)	+= logibm.o
obj-$(CONFIG_MOUSE_MAPLE)	+= maplemouse.o
obj-$(CONFIG_MOUSE_PC110PAD)	+= pc110pad.o
obj-$(CONFIG_MOUSE_PS2)		+= psmouse.o
obj-$(CONFIG_MOUSE_SERIAL)	+= sermouse.o

# The global Rules.make.

include $(TOPDIR)/Rules.make
