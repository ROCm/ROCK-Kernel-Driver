#
# Makefile for the input core drivers.
#

# Objects that export symbols.

export-objs	:= serio.o

# Each configuration option enables a list of files.

obj-$(CONFIG_SERIO)		+= serio.o
obj-$(CONFIG_SERIO_I8042)	+= i8042.o
obj-$(CONFIG_SERIO_PARKBD)	+= parkbd.o
obj-$(CONFIG_SERIO_SERPORT)	+= serport.o
obj-$(CONFIG_SERIO_CT82C710)	+= ct82c710.o
obj-$(CONFIG_SERIO_RPCKBD)	+= rpckbd.o
obj-$(CONFIG_SERIO_Q40KBD)	+= q40kbd.o

# The global Rules.make.

include $(TOPDIR)/Rules.make
