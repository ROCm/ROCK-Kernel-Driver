#
# Makefile for the kernel SAA7146 FULL TS DVB device driver
# and the AV7110 DVB device driver
#

dvb-ttpci-objs := av7110.o av7110_ipack.o av7110_ir.o

obj-$(CONFIG_DVB_BUDGET) += budget-core.o budget.o ttpci-eeprom.o
obj-$(CONFIG_DVB_BUDGET_AV) += budget-core.o budget-av.o ttpci-eeprom.o
obj-$(CONFIG_DVB_BUDGET_CI) += budget-core.o budget-ci.o ttpci-eeprom.o
obj-$(CONFIG_DVB_BUDGET_PATCH) += budget-core.o budget-patch.o ttpci-eeprom.o
obj-$(CONFIG_DVB_AV7110) += dvb-ttpci.o ttpci-eeprom.o

EXTRA_CFLAGS = -Idrivers/media/dvb/dvb-core/
