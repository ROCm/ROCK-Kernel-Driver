all: s390-block.o

CFLAGS += 
O_TARGET := s390-block.o

DASD_OBJS := dasd.o
  ifeq ($(CONFIG_DASD_ECKD),y)
    DASD_OBJS += dasd_eckd.o dasd_3990_erp.o dasd_9343_erp.o
  endif
  ifeq ($(CONFIG_DASD_FBA),y)
    DASD_OBJS += dasd_fba.o dasd_3370_erp.o dasd_9336_erp.o
  endif
  ifeq ($(CONFIG_DASD_MDSK),y)
    DASD_OBJS += dasd_mdsk.o
  endif
#  ifeq ($(CONFIG_DASD_CKD),y)
#    DASD_OBJS += dasd_ckd.o
#  endif

ifeq ($(CONFIG_DASD),y)
  obj-y += $(DASD_OBJS)
else
  ifeq ($(CONFIG_DASD),m)
    obj-m += dasd_mod.o
    D_OBJS += $(DASD_OBJS)
  endif
endif

obj-$(CONFIG_BLK_DEV_XPRAM) += xpram.o

dasd_mod.o: $(D_OBJS)
	$(LD) $(LD_RFLAG) -r -o $@ $(D_OBJS)

include $(TOPDIR)/Rules.make
