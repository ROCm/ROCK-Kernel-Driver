#
# Makefile for the Macintosh-specific device drivers.
#

# Each configuration option enables a list of files.

obj-$(CONFIG_PPC_PMAC)		+= macio_asic.o

obj-$(CONFIG_PMAC_PBOOK)	+= mediabay.o
obj-$(CONFIG_MAC_SERIAL)	+= macserial.o
ifneq ($(CONFIG_MAC),y)
  obj-$(CONFIG_NVRAM)		+= nvram.o
endif
obj-$(CONFIG_MAC_EMUMOUSEBTN)	+= mac_hid.o
obj-$(CONFIG_INPUT_ADBHID)	+= adbhid.o
obj-$(CONFIG_ANSLCD)		+= ans-lcd.o

obj-$(CONFIG_ADB_PMU)		+= via-pmu.o
obj-$(CONFIG_ADB_CUDA)		+= via-cuda.o
obj-$(CONFIG_PMAC_APM_EMU)	+= apm_emu.o

obj-$(CONFIG_ADB)		+= adb.o
obj-$(CONFIG_ADB_MACII)		+= via-macii.o
obj-$(CONFIG_ADB_MACIISI)	+= via-maciisi.o
obj-$(CONFIG_ADB_IOP)		+= adb-iop.o
obj-$(CONFIG_ADB_PMU68K)	+= via-pmu68k.o
obj-$(CONFIG_ADB_MACIO)		+= macio-adb.o
