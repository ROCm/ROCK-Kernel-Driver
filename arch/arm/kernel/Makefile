#
# Makefile for the linux kernel.
#

O_TARGET := kernel.o

ENTRY_OBJ = entry-$(PROCESSOR).o

AFLAGS_head.o := -DTEXTADDR=$(TEXTADDR)

# Object file lists.

obj-y		:= arch.o compat.o dma.o $(ENTRY_OBJ) entry-common.o irq.o   \
		   process.o ptrace.o semaphore.o setup.o signal.o sys_arm.o \
		   time.o traps.o
obj-m		:=
obj-n		:=
obj-		:=

export-objs	:= armksyms.o apm.o dma.o ecard.o fiq.o io.o time.o

obj-$(CONFIG_APM)		+= apm.o
obj-$(CONFIG_ARCH_ACORN)	+= ecard.o time-acorn.o
obj-$(CONFIG_ARCH_CLPS7500)	+= time-acorn.o
obj-$(CONFIG_FOOTBRIDGE)	+= isa.o
obj-$(CONFIG_FIQ)		+= fiq.o
obj-$(CONFIG_MODULES)		+= armksyms.o
obj-$(CONFIG_ARTHUR)		+= arthur.o
obj-$(CONFIG_ISA_DMA)		+= dma-isa.o
obj-$(CONFIG_PCI)		+= bios32.o
obj-$(CONFIG_PCI_HOST_PLX90X0)	+= plx90x0.o
obj-$(CONFIG_PCI_HOST_VIA82C505) += via82c505.o

ifneq ($(MACHINE),ebsa110)
  obj-y		+= io.o
endif

ifeq ($(CONFIG_CPU_32),y)
head-y			:= head.o
obj-$(CONFIG_DEBUG_LL)	+= debug.o
endif

EXTRA_TARGETS := $(head-y) init_task.o

include $(TOPDIR)/Rules.make

# Spell out some dependencies that `make dep' doesn't spot
entry-armv.o: entry-header.S $(TOPDIR)/include/asm-arm/constants.h
entry-armo.o: entry-header.S $(TOPDIR)/include/asm-arm/constants.h
entry-common.o: entry-header.S calls.S $(TOPDIR)/include/asm-arm/constants.h
