#
# Makefile for the linux kernel.
#

# Object file lists.

obj-y			:= dma.o irq.o oldlatches.o \
			   small_page.o

extra-y 		:= head.o

AFLAGS_head.o		:= -DTEXTADDR=$(TEXTADDR)
