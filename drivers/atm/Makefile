# File: drivers/atm/Makefile
#
# Makefile for the Linux network (ATM) device drivers.
#

O_TARGET := atm.o

export-objs := uPD98402.o suni.o idt77105.o

obj-y := atmdev_init.o

obj-$(CONFIG_ATM_ENI) += eni.o suni.o
obj-$(CONFIG_ATM_ZATM) += zatm.o uPD98402.o
obj-$(CONFIG_ATM_NICSTAR) += nicstar.o

ifeq ($(CONFIG_ATM_NICSTAR_USE_SUNI),y)
  obj-$(CONFIG_ATM_NICSTAR) += suni.o
endif

ifeq ($(CONFIG_ATM_NICSTAR_USE_IDT77105),y)
  obj-$(CONFIG_ATM_NICSTAR) += idt77105.o
endif

obj-$(CONFIG_ATM_HORIZON) += horizon.o
obj-$(CONFIG_ATM_AMBASSADOR) += ambassador.o
obj-$(CONFIG_ATM_TCP) += atmtcp.o
obj-$(CONFIG_ATM_IA) += iphase.o suni.o

ifeq ($(CONFIG_ATM_FORE200E_PCA),y)
  FORE200E_FW_OBJS += fore200e_pca_fw.o
  ifeq ($(strip $(CONFIG_ATM_FORE200E_PCA_FW)),"")
    CONFIG_ATM_FORE200E_PCA_DEFAULT_FW := y
  endif
  ifeq ($(CONFIG_ATM_FORE200E_PCA_DEFAULT_FW),y)
#   guess the target endianess to choose the right PCA-200E firmware image
    CONFIG_ATM_FORE200E_PCA_FW := $(shell if test -n "`$(CC) -E -dM ../../include/asm/byteorder.h | grep ' __LITTLE_ENDIAN '`"; then echo pca200e.bin; else echo pca200e_ecd.bin2; fi)
  endif
endif
ifeq ($(CONFIG_ATM_FORE200E_SBA),y)
  FORE200E_FW_OBJS += fore200e_sba_fw.o
  ifeq ($(strip $(CONFIG_ATM_FORE200E_SBA_FW)),"")
    CONFIG_ATM_FORE200E_SBA_DEFAULT_FW := y
  endif
  ifeq ($(CONFIG_ATM_FORE200E_SBA_DEFAULT_FW),y)
    CONFIG_ATM_FORE200E_SBA_FW := sba200e_ecd.bin2
  endif
endif

obj-$(CONFIG_ATM_FORE200E) += fore200e.o $(FORE200E_FW_OBJS)

EXTRA_CFLAGS=-g

include $(TOPDIR)/Rules.make


# FORE Systems 200E-series firmware magic
fore200e_pca_fw.c: $(patsubst "%", %, $(CONFIG_ATM_FORE200E_PCA_FW)) \
	  fore200e_mkfirm
	./fore200e_mkfirm -k -b _fore200e_pca_fw \
	  -i $(CONFIG_ATM_FORE200E_PCA_FW) -o $@
	@ ( \
	  echo 'ifeq ($(strip $(CONFIG_ATM_FORE200E_PCA_FW)), $$(CONFIG_ATM_FORE200E_PCA_FW))'; \
	  echo 'FORE200E_FW_UP_TO_DATE += $@'; \
	  echo 'endif' \
	) >.$@.fw

fore200e_sba_fw.c: $(patsubst "%", %, $(CONFIG_ATM_FORE200E_SBA_FW)) \
	  fore200e_mkfirm
	./fore200e_mkfirm -k -b _fore200e_sba_fw \
	  -i $(CONFIG_ATM_FORE200E_SBA_FW) -o $@
	@ ( \
	  echo 'ifeq ($(strip $(CONFIG_ATM_FORE200E_SBA_FW)), $$(CONFIG_ATM_FORE200E_SBA_FW))'; \
	  echo 'FORE200E_FW_UP_TO_DATE += $@'; \
	  echo 'endif' \
	) >.$@.fw

fore200e_mkfirm: fore200e_mkfirm.c
	$(HOSTCC) $(HOSTCFLAGS) $< -o $@

# deal with the various suffixes of the binary firmware images
%.bin %.bin1 %.bin2: %.data
	objcopy -Iihex $< -Obinary $@.gz
	gzip -df $@.gz

# module build
fore_200e.o: fore200e.o $(FORE200E_FW_OBJS)
	$(LD) -r -o $@ $< $(FORE200E_FW_OBJS)

# firmware dependency stuff taken from drivers/sound/Makefile
FORE200E_FW_UP_TO_DATE :=

FORE200E_FW_FILES := $(wildcard .fore200e_*.fw)
ifneq ($(FORE200E_FW_FILES),)
include $(FORE200E_FW_FILES)
endif

FORE200E_FW_CHANGED := $(filter-out $(FORE200E_FW_UP_TO_DATE), \
                       fore200e_pca_fw.c fore200e_sba_fw.c)

ifneq ($(FORE200E_FW_CHANGED),)
$(FORE200E_FW_CHANGED): dummy
endif
