#
# Makefile for the linux kernel.
#

# Object file lists.

obj-y			:= core.o io.o time.o
obj-m			:=
obj-n			:=
obj-			:=

export-objs		:= io.o

obj-$(CONFIG_LEDS)	+= leds.o

include $(TOPDIR)/Rules.make
