obj-$(CONFIG_FEC_MPC52xx) 	+= fec_mpc52xx.o


fec_mpc52xx-y			+= fec.o sdma_fec.o \
				   sdma_fec_rx_task.o sdma_fec_tx_task.o

fec_mpc52xx-$(CONFIG_FEC_MPC52xx_USE_MDIO) += fec_phy.o

