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
obj-$(CONFIG_PPC_OCP)		+= ocp.o
obj-$(CONFIG_IBM_OCP)		+= ibm_ocp.o
obj-$(CONFIG_44x)		+= ibm44x_common.o
obj-$(CONFIG_440GP)		+= ibm440gp_common.o
obj-$(CONFIG_440GX)		+= ibm440gx_common.o
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
obj-$(CONFIG_PPC_OF)		+= prom_init.o prom.o of_device.o
obj-$(CONFIG_PPC_PMAC)		+= open_pic.o indirect_pci.o
obj-$(CONFIG_POWER4)		+= open_pic2.o
obj-$(CONFIG_PPC_CHRP)		+= open_pic.o indirect_pci.o i8259.o
obj-$(CONFIG_PPC_PREP)		+= open_pic.o indirect_pci.o i8259.o todc_time.o
obj-$(CONFIG_ADIR)		+= i8259.o indirect_pci.o pci_auto.o \
					todc_time.o
obj-$(CONFIG_EBONY)		+= indirect_pci.o pci_auto.o todc_time.o
obj-$(CONFIG_EV64260)		+= gt64260_common.o gt64260_pic.o \
					indirect_pci.o todc_time.o pci_auto.o
obj-$(CONFIG_GEMINI)		+= open_pic.o indirect_pci.o
obj-$(CONFIG_K2)		+= i8259.o indirect_pci.o todc_time.o \
					pci_auto.o
obj-$(CONFIG_LOPEC)		+= pci_auto.o open_pic.o i8259.o todc_time.o
obj-$(CONFIG_MCPN765)		+= todc_time.o indirect_pci.o pci_auto.o \
					open_pic.o i8259.o hawk_common.o
obj-$(CONFIG_MENF1)		+= todc_time.o i8259.o mpc10x_common.o \
					pci_auto.o indirect_pci.o
obj-$(CONFIG_MVME5100)		+= open_pic.o todc_time.o indirect_pci.o \
					i8259.o pci_auto.o hawk_common.o
obj-$(CONFIG_OCOTEA)		+= indirect_pci.o pci_auto.o todc_time.o
obj-$(CONFIG_PAL4)		+= cpc700_pic.o
obj-$(CONFIG_PCORE)		+= todc_time.o i8259.o pci_auto.o
obj-$(CONFIG_POWERPMC250)	+= open_pic.o pci_auto.o
obj-$(CONFIG_PPLUS)		+= hawk_common.o open_pic.o i8259.o \
				   indirect_pci.o todc_time.o pci_auto.o
obj-$(CONFIG_PRPMC750)		+= open_pic.o indirect_pci.o pci_auto.o \
					hawk_common.o
obj-$(CONFIG_HARRIER)		+= harrier.o
obj-$(CONFIG_PRPMC800)		+= open_pic.o indirect_pci.o pci_auto.o
obj-$(CONFIG_SANDPOINT)		+= i8259.o open_pic.o pci_auto.o todc_time.o
obj-$(CONFIG_SPRUCE)		+= cpc700_pic.o indirect_pci.o pci_auto.o \
				   todc_time.o
obj-$(CONFIG_8260)		+= m8260_setup.o ppc8260_pic.o
ifeq ($(CONFIG_PPC_GEN550),y)
obj-$(CONFIG_KGDB)		+= gen550_kgdb.o gen550_dbg.o
obj-$(CONFIG_SERIAL_TEXT_DEBUG)	+= gen550_dbg.o
endif
obj-$(CONFIG_BOOTX_TEXT)	+= btext.o
obj-$(CONFIG_MPC10X_BRIDGE)     += mpc10x_common.o indirect_pci.o
obj-$(CONFIG_40x)		+= dcr.o
obj-$(CONFIG_BOOKE)		+= dcr.o
