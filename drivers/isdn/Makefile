# Makefile for the kernel ISDN subsystem and device drivers.

# The target object and module list name.

O_TARGET	:= vmlinux-obj.o

# Object files in subdirectories

mod-subdirs				:= i4l hisax capi eicon
subdir-$(CONFIG_ISDN)			+= i4l
subdir-$(CONFIG_ISDN_CAPI)		+= capi
subdir-$(CONFIG_ISDN_CAPI)		+= hardware
subdir-$(CONFIG_ISDN_DIVERSION)		+= divert
subdir-$(CONFIG_ISDN_DRV_HISAX)		+= hisax
subdir-$(CONFIG_ISDN_DRV_ICN)		+= icn
subdir-$(CONFIG_ISDN_DRV_PCBIT)		+= pcbit
subdir-$(CONFIG_ISDN_DRV_SC)		+= sc
subdir-$(CONFIG_ISDN_DRV_LOOP)		+= isdnloop
subdir-$(CONFIG_ISDN_DRV_ACT2000)	+= act2000
subdir-$(CONFIG_ISDN_DRV_EICON)		+= eicon
subdir-$(CONFIG_HYSDN)			+= hysdn
subdir-$(CONFIG_ISDN_DRV_TPAM)		+= tpam

obj-y += $(addsuffix /vmlinux-obj.o, $(subdir-y))

# The global Rules.make.

include $(TOPDIR)/Rules.make
