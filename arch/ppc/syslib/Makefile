#
# Makefile for the linux kernel.
#

ifdef CONFIG_PPC64BRIDGE
EXTRA_AFLAGS		:= -Wa,-mppc64bridge
endif
ifdef CONFIG_4xx
EXTRA_AFLAGS		:= -Wa,-m405
endif

CFLAGS_prom_init.o      += -mrelocatable-lib
CFLAGS_btext.o          += -mrelocatable-lib

obj-$(CONFIG_PPCBUG_NVRAM)	+= prep_nvram.o
ifeq ($(CONFIG_4xx),y)
obj-$(CONFIG_4xx)		+= ppc4xx_pic.o
obj-$(CONFIG_40x)		+= ppc4xx_setup.o
obj-$(CONFIG_GEN_RTC)		+= todc_time.o
obj-$(CONFIG_KGDB)		+= ppc4xx_kgdb.o
ifeq ($(CONFIG_40x),y)
obj-$(CONFIG_KGDB)		+= ppc4xx_kgdb.o
obj-$(CONFIG_PCI)		+= indirect_pci.o pci_auto.o ppc405_pci.o
endif
endif
obj-$(CONFIG_8xx)		+= m8xx_setup.o ppc8xx_pic.o
ifeq ($(CONFIG_8xx),y)
obj-$(CONFIG_PCI)		+= qspan_pci.o i8259.o
endif
obj-$(CONFIG_ALL_PPC)		+= prom_init.o prom.o open_pic.o \
					indirect_pci.o i8259.o
obj-$(CONFIG_ADIR)		+= i8259.o indirect_pci.o pci_auto.o \
					todc_time.o
obj-$(CONFIG_EV64260)		+= gt64260_common.o gt64260_pic.o \
					indirect_pci.o todc_time.o pci_auto.o
obj-$(CONFIG_GEMINI)		+= open_pic.o i8259.o indirect_pci.o
obj-$(CONFIG_K2)		+= i8259.o indirect_pci.o todc_time.o \
					pci_auto.o
obj-$(CONFIG_LOPEC)		+= mpc10x_common.o indirect_pci.o pci_auto.o \
					open_pic.o i8259.o todc_time.o
obj-$(CONFIG_MCPN765)		+= todc_time.o indirect_pci.o pci_auto.o \
					open_pic.o i8259.o pplus_common.o
obj-$(CONFIG_MENF1)		+= todc_time.o i8259.o mpc10x_common.o \
					pci_auto.o indirect_pci.o
obj-$(CONFIG_MVME5100)		+= open_pic.o todc_time.o indirect_pci.o \
					i8259.o pci_auto.o pplus_common.o
obj-$(CONFIG_PAL4)		+= cpc700_pic.o
obj-$(CONFIG_PCORE)		+= mpc10x_common.o todc_time.o i8259.o \
					indirect_pci.o pci_auto.o
obj-$(CONFIG_POWERPMC250)	+= open_pic.o mpc10x_common.o \
					indirect_pci.o pci_auto.o
obj-$(CONFIG_PPLUS)		+= pplus_common.o open_pic.o i8259.o \
				   indirect_pci.o todc_time.o pci_auto.o
obj-$(CONFIG_PRPMC750)		+= open_pic.o indirect_pci.o pci_auto.o \
					pplus_common.o
obj-$(CONFIG_PRPMC800)		+= open_pic.o indirect_pci.o pci_auto.o \
					pplus_common.o harrier.o
obj-$(CONFIG_SANDPOINT)		+= i8259.o open_pic.o mpc10x_common.o \
					pci_auto.o indirect_pci.o todc_time.o
obj-$(CONFIG_SPRUCE)		+= cpc700_pic.o indirect_pci.o pci_auto.o \
				   todc_time.o
obj-$(CONFIG_ZX4500)		+= indirect_pci.o pci_auto.o mpc10x_common.o \
					i8259.o open_pic.o
obj-$(CONFIG_8260)		+= m8260_setup.o ppc8260_pic.o
obj-$(CONFIG_BOOTX_TEXT)	+= btext.o

