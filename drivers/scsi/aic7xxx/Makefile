#
# Makefile for the Linux aic7xxx SCSI driver.
#

O_TARGET := aic7xxx_drv.o
MOD_TARGET = aic7xxx.o

# Core files
obj-$(CONFIG_SCSI_AIC7XXX)	+= aic7xxx.o aic7xxx_93cx6.o aic7770.o

# Platform Specific Files
obj-$(CONFIG_SCSI_AIC7XXX)	+= aic7xxx_linux.o aic7xxx_proc.o aic7770_linux.o

# PCI Specific Files
ifeq ($(CONFIG_PCI),y)
  # Core PCI files
  obj-$(CONFIG_SCSI_AIC7XXX) += aic7xxx_pci.o
  # Platform Specific PCI Files
  obj-$(CONFIG_SCSI_AIC7XXX) += aic7xxx_linux_pci.o
endif

#EXTRA_CFLAGS += -g

include $(TOPDIR)/Rules.make

ifeq ($(CONFIG_AIC7XXX_BUILD_FIRMWARE),y)
aic7xxx_seq.h aic7xxx_reg.h: aic7xxx.seq aic7xxx.reg aicasm/aicasm
	aicasm/aicasm -I. -r aic7xxx_reg.h -o aic7xxx_seq.h aic7xxx.seq
endif

aicasm/aicasm: aicasm/*.[chyl]
	$(MAKE) -C aicasm
