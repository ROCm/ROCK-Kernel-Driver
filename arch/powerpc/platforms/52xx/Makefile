#
# Makefile for 52xx based boards
#
ifeq ($(CONFIG_PPC_MERGE),y)
obj-y				+= mpc52xx_pic.o mpc52xx_common.o
obj-$(CONFIG_PCI)		+= mpc52xx_pci.o
obj-$(CONFIG_PPC_BESTCOMM) 	+= bestcomm.o
obj-$(CONFIG_FEC_MPC52xx)	+= sdma_fec_rx_task.o sdma_fec_tx_task.o fec.o
endif

obj-$(CONFIG_PPC_EFIKA)		+= efika.o
obj-$(CONFIG_PPC_LITE5200)	+= lite5200.o
