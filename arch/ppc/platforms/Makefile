# BK Id: %F% %I% %G% %U% %#%
#
#
# Makefile for the linux kernel.
#

ifdef CONFIG_PPC64BRIDGE
EXTRA_AFLAGS		:= -Wa,-mppc64bridge
endif
ifdef CONFIG_40x
EXTRA_AFLAGS		:= -Wa,-m405
endif

# Extra CFLAGS so we don't have to do relative includes
CFLAGS_pmac_setup.o	+= -I$(TOPDIR)/arch/$(ARCH)/mm

O_TARGET := platform.o

export-objs			:= prep_setup.o

obj-$(CONFIG_CEDER)		+= ceder.o ibmnp405l.o
obj-$(CONFIG_CPCI405)		+= cpci405.o ibm405gp.o
obj-$(CONFIG_EP405)		+= ep405.o ibm405gp.o 
obj-$(CONFIG_REDWOOD_4)		+= redwood.o ibmstb3.o
obj-$(CONFIG_REDWOOD_5)		+= redwood5.o ibmstb4.o
obj-$(CONFIG_WALNUT)		+= walnut.o ibm405gp.o
obj-$(CONFIG_ASH)		+= ash.o ibmnp405h.o
obj-$(CONFIG_APUS)		+= apus_setup.o
ifeq ($(CONFIG_APUS),y)
obj-$(CONFIG_PCI)		+= apus_pci.o
endif
obj-$(CONFIG_ALL_PPC)		+= pmac_pic.o pmac_setup.o pmac_time.o \
					pmac_feature.o pmac_pci.o chrp_setup.o\
					chrp_time.o chrp_pci.o prep_pci.o \
					prep_time.o prep_setup.o
ifeq ($(CONFIG_ALL_PPC),y)
obj-$(CONFIG_NVRAM)		+= pmac_nvram.o
endif
obj-$(CONFIG_PMAC_BACKLIGHT)	+= pmac_backlight.o
obj-$(CONFIG_PMAC_PBOOK)	+= sleep.o
obj-$(CONFIG_PPC_RTAS)		+= error_log.o proc_rtas.o
obj-$(CONFIG_PREP_RESIDUAL)	+= residual.o
obj-$(CONFIG_ADIR)		+= adir_setup.o adir_pic.o adir_pci.o
obj-$(CONFIG_EV64260)		+= ev64260_setup.o
obj-$(CONFIG_GEMINI)		+= gemini_pci.o gemini_setup.o gemini_prom.o
obj-$(CONFIG_K2)		+= k2_setup.o k2_pci.o
obj-$(CONFIG_LOPEC)		+= lopec_setup.o lopec_pci.o
obj-$(CONFIG_MCPN765)		+= mcpn765_setup.o mcpn765_pci.o
obj-$(CONFIG_MENF1)		+= menf1_setup.o menf1_pci.o
obj-$(CONFIG_MVME5100)		+= mvme5100_setup.o mvme5100_pci.o
obj-$(CONFIG_PCORE)		+= pcore_setup.o pcore_pci.o
obj-$(CONFIG_POWERPMC250)	+= powerpmc250.o
obj-$(CONFIG_PPLUS)		+= pplus_pci.o pplus_setup.o
obj-$(CONFIG_PRPMC750)		+= prpmc750_setup.o prpmc750_pci.o
obj-$(CONFIG_PRPMC800)		+= prpmc800_setup.o prpmc800_pci.o
obj-$(CONFIG_SANDPOINT)		+= sandpoint_setup.o sandpoint_pci.o
obj-$(CONFIG_SPRUCE)		+= spruce_setup.o spruce_pci.o cpc700_pic.o
obj-$(CONFIG_ZX4500)		+= zx4500_setup.o zx4500_pci.o
obj-$(CONFIG_PPC_ISERIES)	+= iSeries_setup.o iSeries_time.o \
					iSeries_dma.o iSeries_pic.o

ifeq ($(CONFIG_SMP),y)
obj-$(CONFIG_ALL_PPC)		+= pmac_smp.o chrp_smp.o
obj-$(CONFIG_PPC_ISERIES)	+= iSeries_smp.o
endif

include $(TOPDIR)/Rules.make
