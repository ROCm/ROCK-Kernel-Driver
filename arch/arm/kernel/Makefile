#
# Makefile for the linux kernel.
#
# Note! Dependencies are done automagically by 'make dep', which also
# removes any old dependencies. DON'T put your own dependencies here
# unless it's something special (ie not a .c file).

USE_STANDARD_AS_RULE := true

HEAD_OBJ  = head-$(PROCESSOR).o
ENTRY_OBJ = entry-$(PROCESSOR).o

AFLAGS_head-armv.o := -DTEXTADDR=$(TEXTADDR)
AFLAGS_head-armo.o := -DTEXTADDR=$(TEXTADDR)

O_OBJS_arc        = dma-arc.o oldlatches.o
O_OBJS_rpc        = dma-rpc.o
O_OBJS_footbridge = dma-footbridge.o isa.o
O_OBJS_l7200      = fiq.o

pci-nexuspci	  = plx90x0.o
pci-footbridge	  = dec21285.o
pci-shark	  = via82c505.o

pci-$(CONFIG_ARCH_NEXUSPCI) += ftv-pci.o


O_TARGET := kernel.o

# Object file lists.

obj-y		:= arch.o dma.o $(ENTRY_OBJ) irq.o process.o ptrace.o    \
		   semaphore.o setup.o signal.o sys_arm.o time.o traps.o \
		   $(O_OBJS_$(MACHINE))
obj-m		:=
obj-n		:=
obj-		:=

export-objs	:= armksyms.o dma.o ecard.o fiq.o oldlatches.o time.o

no-irq-arch	:= $(CONFIG_ARCH_INTEGRATOR) $(CONFIG_ARCH_CLPS711X) \
		   $(CONFIG_ARCH_FOOTBRIDGE) $(CONFIG_ARCH_EBSA110)

ifneq ($(findstring y,$(no-irq-arch)),y)
  obj-y		+= irq-arch.o
endif

obj-$(CONFIG_ARCH_ACORN) += ecard.o fiq.o time-acorn.o
obj-$(CONFIG_DEBUG_LL)	+= debug-$(PROCESSOR).o
obj-$(CONFIG_MODULES)	+= armksyms.o
obj-$(CONFIG_ARTHUR)	+= arthur.o
obj-$(CONFIG_ISA_DMA)	+= dma-isa.o
obj-$(CONFIG_PCI)	+= bios32.o $(pci-$(MACHINE)) $(pci-y)

all: kernel.o $(HEAD_OBJ) init_task.o

include $(TOPDIR)/Rules.make

# Spell out some dependencies that `make dep' doesn't spot
entry-armv.o: calls.S ../lib/constants.h
entry-armo.o: calls.S ../lib/constants.h
