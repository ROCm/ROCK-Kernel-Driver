#
# Makefile for the linux kernel.
#

# Object file lists.

obj-y			:= arch.o dma.o fault.o irq.o mm.o oldlatches.o \
			   small_page.o

obj-$(CONFIG_DEBUG_LL)	+= debug.o

extra-y 		:= head.o

AFLAGS_head.o		:= -DTEXTADDR=$(TEXTADDR)
