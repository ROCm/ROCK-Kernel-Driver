#
# S/390 block devices
#

export-objs := dasd.o

dasd_eckd_mod-objs := dasd_eckd.o dasd_3990_erp.o dasd_9343_erp.o
dasd_fba_mod-objs  := dasd_fba.o dasd_3370_erp.o dasd_9336_erp.o
dasd_diag_mod-objs := dasd_diag.o
dasd_mod-objs      := dasd.o

obj-$(CONFIG_DASD) += dasd_mod.o
obj-$(CONFIG_DASD_ECKD) += dasd_eckd_mod.o
obj-$(CONFIG_DASD_FBA)  += dasd_fba_mod.o
obj-$(CONFIG_DASD_DIAG) += dasd_diag_mod.o
obj-$(CONFIG_BLK_DEV_XPRAM) += xpram.o

include $(TOPDIR)/Rules.make
