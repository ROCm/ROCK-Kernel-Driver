#
# Makefile for the linux kernel.
#
# Note! Dependencies are done automagically by 'make dep', which also
# removes any old dependencies. DON'T put your own dependencies here
# unless it's something special (ie not a .c file).

USE_STANDARD_AS_RULE := true

HEAD_OBJ  = head-$(PROCESSOR).o
ENTRY_OBJ = entry-$(PROCESSOR).o

AFLAGS_head-armv.o := -DTEXTADDR=$(TEXTADDR) -traditional
AFLAGS_head-armo.o := -DTEXTADDR=$(TEXTADDR) -traditional

O_OBJS_arc        = dma-arc.o oldlatches.o
O_OBJS_rpc        = dma-rpc.o
O_OBJS_footbridge = dma-footbridge.o isa.o
O_OBJS_l7200      = fiq.o

leds-ebsa110	  = leds-ebsa110.o

pci-nexuspci	  = plx90x0.o
pci-footbridge	  = dec21285.o
pci-shark	  = via82c505.o

pci-$(CONFIG_ARCH_NEXUSPCI) += ftv-pci.o


O_TARGET := kernel.o

# Object file lists.

obj-y			:= arch.o dma.o $(ENTRY_OBJ) irq.o process.o ptrace.o \
			   semaphore.o setup.o signal.o sys_arm.o time.o \
			   traps.o $(O_OBJS_$(MACHINE))
obj-m			:=
obj-n			:=
obj-			:=

export-objs		:= armksyms.o dma.o ecard.o \
			   $(leds-$(MACHINE)) oldlatches.o \
			   time.o

obj-$(CONFIG_ARCH_ACORN) += ecard.o fiq.o time-acorn.o
obj-$(CONFIG_DEBUG_LL)	+= debug-$(PROCESSOR).o
obj-$(CONFIG_MODULES)	+= armksyms.o
obj-$(CONFIG_LEDS)	+= $(leds-$(MACHINE))
obj-$(CONFIG_ARTHUR)	+= arthur.o
obj-$(CONFIG_ISA_DMA)	+= dma-isa.o

obj-$(CONFIG_PCI)	+= bios32.o $(pci-$(MACHINE)) $(pci-y)

# Files that are both resident and modular; remove from modular.

obj-m		:= $(filter-out $(obj-y), $(obj-m))

# Translate to Rules.make lists.

O_OBJS		:= $(filter-out $(export-objs), $(obj-y))
OX_OBJS 	:= $(filter	$(export-objs), $(obj-y))
M_OBJS		:= $(sort $(filter-out $(export-objs), $(obj-m)))
MX_OBJS 	:= $(sort $(filter     $(export-objs), $(obj-m)))

all: kernel.o $(HEAD_OBJ) init_task.o

include $(TOPDIR)/Rules.make

# Spell out some dependencies that `make dep' doesn't spot
entry-armv.o: calls.S ../lib/constants.h
entry-armo.o: calls.S ../lib/constants.h
