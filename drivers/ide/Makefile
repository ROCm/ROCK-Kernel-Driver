#
# Makefile for the kernel ata, atapi, and ide block device drivers.
#
# 12 September 2000, Bartlomiej Zolnierkiewicz <bkz@linux-ide.org>
# Rewritten to use lists instead of if-statements.
#
# Note : at this point, these files are compiled on all systems.
# In the future, some of these should be built conditionally.
#
export-objs := ide-iops.o ide-taskfile.o ide-proc.o ide.o ide-probe.o ide-dma.o ide-lib.o setup-pci.o

# First come modules that register themselves with the core
obj-$(CONFIG_BLK_DEV_IDEPCI)		+= pci/

# Core IDE code - must come before legacy

obj-$(CONFIG_BLK_DEV_IDE)		+= ide-probe.o ide-geometry.o ide-iops.o ide-taskfile.o ide.o ide-lib.o
obj-$(CONFIG_BLK_DEV_IDEDISK)		+= ide-disk.o
obj-$(CONFIG_BLK_DEV_IDECD)		+= ide-cd.o
obj-$(CONFIG_BLK_DEV_IDETAPE)		+= ide-tape.o
obj-$(CONFIG_BLK_DEV_IDEFLOPPY)		+= ide-floppy.o

obj-$(CONFIG_BLK_DEV_IDEPCI)		+= setup-pci.o
obj-$(CONFIG_BLK_DEV_IDEDMA_PCI)	+= ide-dma.o
obj-$(CONFIG_BLK_DEV_IDE_TCQ)		+= ide-tcq.o
obj-$(CONFIG_BLK_DEV_ISAPNP)		+= ide-pnp.o

ifeq ($(CONFIG_BLK_DEV_IDE),y)
obj-$(CONFIG_PROC_FS)			+= ide-proc.o
endif

obj-$(CONFIG_BLK_DEV_IDE)		+= legacy/ ppc/ arm/

include $(TOPDIR)/Rules.make
