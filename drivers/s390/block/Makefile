all: s390-block.o

CFLAGS += 
O_TARGET := s390-block.o
O_OBJS   :=         
M_OBJS   :=

ifeq ($(CONFIG_DASD),y)
  O_OBJS += dasd.o dasd_ccwstuff.o dasd_erp.o
  ifeq ($(CONFIG_PROC_FS),y)
   O_OBJS += dasd_proc.o dasd_profile.o
  endif 
  ifeq ($(CONFIG_DASD_ECKD),y)
   O_OBJS += dasd_eckd.o
  endif 
  ifeq ($(CONFIG_DASD_MDSK),y)
   O_OBJS += dasd_mdsk.o
  endif 
#  ifeq ($(CONFIG_DASD_CKD),y)
#   O_OBJS += dasd_ckd.o
#  endif 
endif

ifeq ($(CONFIG_MDISK),y)
  O_OBJS += mdisk.o
endif

dasd_mod.o: $(D_OBJS)
	$(LD) $(LD_RFLAG) -r -o $@ $+

include $(TOPDIR)/Rules.make
