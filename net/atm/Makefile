#
# Makefile for the ATM Protocol Families.
#

mpoa-objs	:= mpc.o mpoa_caches.o mpoa_proc.o

obj-$(CONFIG_ATM) := addr.o pvc.o signaling.o svc.o common.o atm_misc.o raw.o resources.o

obj-$(CONFIG_ATM_CLIP) += clip.o ipcommon.o
obj-$(CONFIG_ATM_BR2684) += br2684.o ipcommon.o
obj-$(CONFIG_NET_SCH_ATM) += ipcommon.o
obj-$(CONFIG_PROC_FS) += proc.o

obj-$(CONFIG_ATM_LANE) += lec.o
obj-$(CONFIG_ATM_MPOA) += mpoa.o
obj-$(CONFIG_PPPOATM) += pppoatm.o
