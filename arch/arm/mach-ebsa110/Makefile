#
# Makefile for the linux kernel.
#
# Note! Dependencies are done automagically by 'make dep', which also
# removes any old dependencies. DON'T put your own dependencies here
# unless it's something special (ie not a .c file).

O_TARGET		:= ebsa110.o

# Object file lists.

obj-y			:= core.o io.o time.o
obj-m			:=
obj-n			:=
obj-			:=

export-objs		:= io.o

obj-$(CONFIG_LEDS)	+= leds.o

include $(TOPDIR)/Rules.make
