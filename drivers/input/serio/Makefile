#
# Makefile for the input core drivers.
#

# The target object and module list name.

O_TARGET	:= seriodrv.o

# Objects that export symbols.

export-objs	:= serio.o

# Each configuration option enables a list of files.

obj-$(CONFIG_SERIO)		+= serio.o
obj-$(CONFIG_SERIO_SERPORT)	+= serport.o

# The global Rules.make.

include $(TOPDIR)/Rules.make
