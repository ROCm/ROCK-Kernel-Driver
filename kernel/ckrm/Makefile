#
# Makefile for the ckrm Module
#

ifeq ($(CONFIG_CKRM),y)
	obj-y = ckrm.o ckrmutils.o
else
	obj-y = ckrmstub.o
endif

obj-$(CONFIG_CKRM_RES_DUMMY) += ckrm_dummy.o ckrm_socketaq.o

