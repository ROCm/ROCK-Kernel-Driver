#
# Makefile for the Linux aic7xxx SCSI driver.
#

obj-$(CONFIG_SCSI_AIC7XXX)	+= aic7xxx.o
ifeq ($(CONFIG_PCI),y)
obj-$(CONFIG_SCSI_AIC79XX)	+= aic79xx.o
endif

# Core files
aic7xxx-objs	+= aic7xxx_core.o aic7xxx_93cx6.o aic7770.o

# Platform Specific Files
aic7xxx-objs	+= aic7xxx_osm.o aic7xxx_proc.o aic7770_osm.o
ifeq ($(CONFIG_AIC7XXX_REG_PRETTY_PRINT),y)
aic7xxx-objs	+=  aic7xxx_reg_print.o
endif

# PCI Specific Files
ifeq ($(CONFIG_PCI),y)
  # Core PCI files
  aic7xxx-objs	+= aic7xxx_pci.o
  # Platform Specific PCI Files
  aic7xxx-objs	+= aic7xxx_osm_pci.o
endif

# Platform Specific U320 Files
aic79xx-objs	= aic79xx_osm.o aic79xx_proc.o aic79xx_osm_pci.o
# Core Files
aic79xx-objs	+= aic79xx_core.o aic79xx_pci.o
ifeq ($(CONFIG_AIC79XX_REG_PRETTY_PRINT),y)
aic79xx-objs	+= aic79xx_reg_print.o
endif

#EXTRA_CFLAGS += -g

clean := aic7xxx_seq.h aic7xxx_reg.h aic7xxx_reg_print.c
clean += aic79xx_seq.h aic79xx_reg.h aic79xx_reg_print.c

#
# Can't this be done by calling into the aicasm Makefile instead
# of copying the clean target from there?
#mrproper := $(addprefix \
#aicasm/,aicasm_gram.c aicasm_scan.c y.tab.h aicdb.h y.output aicasm)
 

include $(TOPDIR)/Rules.make

# Dependencies for generated files need to be listed explicitly

$(obj)/aic7xxx_core.o: $(obj)/aic7xxx_seq.h
$(obj)/aic79xx_core.o: $(obj)/aic79xx_seq.h

$(addprefix $(obj)/,$(aic7xxx-objs)): $(obj)/aic7xxx_reg.h
$(addprefix $(obj)/,$(aic79xx-objs)): $(obj)/aic79xx_reg.h

ifeq ($(CONFIG_AIC7XXX_BUILD_FIRMWARE),y)
aic7xxx_gen = $(obj)/aic7xxx_seq.h $(obj)/aic7xxx_reg.h
ifeq ($(CONFIG_AIC7XXX_REG_PRETTY_PRINT),y)
aic7xxx_gen += $(obj)/aic7xxx_reg_print.c
aic7xxx_asm_cmd = $(obj)/aicasm/aicasm -I. -r $(obj)/aic7xxx_reg.h \
		 -p aic7xxx_reg_print.c -i aic7xxx_osm.h	   \
		 -o $(obj)/aic7xxx_seq.h $(src)/aic7xxx.seq
else
aic7xxx_asm_cmd = $(obj)/aicasm/aicasm -I. -r $(obj)/aic7xxx_reg.h \
		 -o $(obj)/aic7xxx_seq.h $(src)/aic7xxx.seq
endif

$(aic7xxx_gen): $(src)/aic7xxx.seq $(src)/aic7xxx.reg $(obj)/aicasm/aicasm
	$(aic7xxx_asm_cmd)
endif

ifeq ($(CONFIG_AIC79XX_BUILD_FIRMWARE),y)
aic79xx_gen = $(obj)/aic79xx_seq.h $(obj)/aic79xx_reg.h
ifeq ($(CONFIG_AIC79XX_REG_PRETTY_PRINT),y)
aic79xx_gen += $(obj)/aic79xx_reg_print.c
aic79xx_asm_cmd = $(obj)/aicasm/aicasm -I. -r $(obj)/aic79xx_reg.h \
		 -p aic79xx_reg_print.c -i aic79xx_osm.h	   \
		 -o $(obj)/aic79xx_seq.h $(src)/aic79xx.seq
else
aic79xx_asm_cmd = $(obj)/aicasm/aicasm -I. -r $(obj)/aic79xx_reg.h \
		 -o $(obj)/aic79xx_seq.h $(src)/aic79xx.seq
endif
$(aic79xx_gen): $(src)/aic79xx.seq $(src)/aic79xx.reg $(obj)/aicasm/aicasm
	$(aic79xx_asm_cmd)

$(obj)/aicasm/aicasm: $(src)/aicasm/*.[chyl]
	$(MAKE) -C $(src)/aicasm

endif
