#
# Makefile for the kernel SAA7146 FULL TS DVB device driver
# and the AV7110 DVB device driver
#

dvb-ttpci-budget-objs := budget.o
dvb-ttpci-budget-av-objs := budget-av.o
dvb-ttpci-budget-ci-objs := budget-ci.o
dvb-ttpci-budget-patch-objs := budget-patch.o
dvb-ttpci-objs := av7110.o av7110_ipack.o av7110_ir.o

obj-$(CONFIG_DVB_BUDGET) += budget-core.o dvb-ttpci-budget.o
obj-$(CONFIG_DVB_BUDGET_CI) += budget-core.o dvb-ttpci-budget-ci.o
obj-$(CONFIG_DVB_BUDGET_AV) += budget-core.o dvb-ttpci-budget-av.o
obj-$(CONFIG_DVB_BUDGET_PATCH) += budget-core.o dvb-ttpci-budget-patch.o
obj-$(CONFIG_DVB_AV7110) += dvb-ttpci.o

EXTRA_CFLAGS = -Idrivers/media/dvb/dvb-core/
