#
# S/390 block devices
#

O_TARGET := s390-block.o

list-multi := dasd_mod.o
export-objs := dasd.o

dasd_mod-$(CONFIG_DASD_ECKD) += dasd_eckd.o dasd_3990_erp.o dasd_9343_erp.o
dasd_mod-$(CONFIG_DASD_FBA) += dasd_fba.o dasd_3370_erp.o dasd_9336_erp.o
dasd_mod-$(CONFIG_DASD_DIAG) += dasd_diag.o
dasd_mod-objs := dasd.o $(dasd_mod-y)

obj-$(CONFIG_DASD) += dasd_mod.o
obj-$(CONFIG_BLK_DEV_XPRAM) += xpram.o

include $(TOPDIR)/Rules.make

dasd_mod.o: $(dasd_mod-objs)
	$(LD) -r -o $@ $(dasd_mod-objs)

