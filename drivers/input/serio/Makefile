#
# Makefile for the input core drivers.
#

# Objects that export symbols.

export-objs	:= serio.o

# Each configuration option enables a list of files.

obj-$(CONFIG_SERIO)		+= serio.o
obj-$(CONFIG_SERIO_SERPORT)	+= serport.o

# The global Rules.make.

include $(TOPDIR)/Rules.make
