#
# LSI mpt fusion
#

# csmi ioctls enable
EXTRA_CFLAGS += -DCPQ_CIM
EXTRA_CFLAGS += -DDIAG_BUFFER_SUPPORT

EXTRA_CFLAGS += -DCONFIG_FUSION_LOGGING

# enable verbose logging
# CONFIG_FUSION_LOGGING needs to be enabled in Kconfig
#EXTRA_CFLAGS += -DMPT_DEBUG_VERBOSE

obj-$(CONFIG_FUSION_SPI)	+= mptbase.o mptscsih.o mptspi.o
obj-$(CONFIG_FUSION_FC)		+= mptbase.o mptscsih.o mptfc.o
obj-$(CONFIG_FUSION_SAS)	+= mptbase.o mptscsih.o mptsas.o
obj-$(CONFIG_FUSION_CTL)	+= mptctl.o
obj-$(CONFIG_FUSION_LAN)	+= mptlan.o
