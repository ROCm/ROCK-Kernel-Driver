#
# Makefile for the Linux aic7xxx SCSI driver.
#

obj-$(CONFIG_SCSI_AIC7XXX)	+= aic7xxx.o

# Core files
aic7xxx-objs	+= aic7xxx_core.o aic7xxx_93cx6.o aic7770.o

# Platform Specific Files
aic7xxx-objs	+= aic7xxx_linux.o aic7xxx_proc.o aic7770_linux.o

# PCI Specific Files
ifeq ($(CONFIG_PCI),y)
  # Core PCI files
  aic7xxx-objs	+= aic7xxx_pci.o
  # Platform Specific PCI Files
  aic7xxx-objs	+= aic7xxx_linux_pci.o
endif

#EXTRA_CFLAGS += -g

include $(TOPDIR)/Rules.make

# Dependencies for generated files need to be listed explicitly

$(obj)/aic7xxx_core.o: $(obj)/aic7xxx_seq.h

$(addprefix $(obj)/,$(aic7xxx-objs)): $(obj)/aic7xxx_reg.h

ifeq ($(CONFIG_AIC7XXX_BUILD_FIRMWARE),y)

$(obj)/aic7xxx_seq.h $(obj)/aic7xxx_reg.h: $(src)/aic7xxx.seq \
					   $(src)/aic7xxx.reg \
					   $(obj)/aicasm/aicasm
	$(obj)/aicasm/aicasm -I. -r $(obj)/aic7xxx_reg.h \
				 -o $(obj)/aic7xxx_seq.h $(src)/aic7xxx.seq

$(obj)/aicasm/aicasm: $(src)/aicasm/*.[chyl]
	$(MAKE) -C $(src)/aicasm

endif

