#
# Makefile for the ATM Protocol Families.
#

O_TARGET	:= atm.o

export-objs 	:= common.o atm_misc.o raw.o resources.o ipcommon.o proc.o

mpoa-objs	:= mpc.o mpoa_caches.o mpoa_proc.o

obj-$(CONFIG_ATM) := addr.o pvc.o signaling.o svc.o common.o atm_misc.o raw.o resources.o

ifeq ($(CONFIG_ATM_CLIP),y)
obj-y += clip.o
NEED_IPCOM = ipcommon.o
endif

ifeq ($(CONFIG_NET_SCH_ATM),y)
NEED_IPCOM = ipcommon.o
endif

obj-y += $(NEED_IPCOM)

ifeq ($(CONFIG_PROC_FS),y)
obj-y += proc.o
endif

obj-$(CONFIG_ATM_LANE) += lec.o
obj-$(CONFIG_ATM_MPOA) += mpoa.o
obj-$(CONFIG_PPPOATM) += pppoatm.o

include $(TOPDIR)/Rules.make
