#
# Makefile for the kernel mmc device drivers.
#

#
# Core
#
obj-$(CONFIG_MMC)		+= mmc_core.o

#
# Media drivers
#
obj-$(CONFIG_MMC_BLOCK)		+= mmc_block.o

#
# Host drivers
#

mmc_core-y := mmc.o mmc_queue.o mmc_sysfs.o
