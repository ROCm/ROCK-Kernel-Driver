#
# Makefile for the linux kernel.
#

ENTRY_OBJ = entry.o

# Object file lists.

obj-y		:= compat.o dma.o entry.o irq.o   \
		   process.o ptrace.o semaphore.o setup.o signal.o sys_arm.o \
		   time.o traps.o ecard.o time-acorn.o dma.o \
		   ecard.o fiq.o time.o

obj-$(CONFIG_FIQ)		+= fiq.o
obj-$(CONFIG_MODULES)		+= armksyms.o

extra-y := init_task.o vmlinux.lds

