#
# Makefile for the linux m68k-specific parts of the memory manager.
#

obj-y		:= init.o fault.o extable.o hwtest.o

ifndef CONFIG_SUN3
obj-y		+= kmap.o memory.o motorola.o
else
obj-y		+= sun3kmap.o sun3mmu.o
endif
