#
# Makefile for the linux kernel.
#
# Note! Dependencies are done automagically by 'make dep', which also
# removes any old dependencies. DON'T put your own dependencies here
# unless it's something special (ie not a .c file).

USE_STANDARD_AS_RULE := true

O_TARGET		:= arc.o

# Object file lists.

obj-y			:= arch.o dma.o fault.o irq.o mm.o oldlatches.o \
			   small_page.o
obj-m			:=
obj-n			:=
obj-			:=

export-objs		:= oldlatches.o

obj-$(CONFIG_DEBUG_LL)	+= debug.o

all: arc.o head.o

include $(TOPDIR)/Rules.make

AFLAGS_head.o := -DTEXTADDR=$(TEXTADDR)
