#
# Makefile for CKRM 
#

obj-$(CONFIG_CKRM_RBCE) += rbce.o
rbce-objs := rbcemod.o rbce_fs.o

obj-$(CONFIG_CKRM_CRBCE) += crbce.o
crbce-objs := crbcemod.o rbce_fs.o

CFLAGS_crbcemod.o  += -DRBCE_EXTENSION	    # compile rbcemod.c into crbce
CFLAGS_crbcemod.o  += -DRBCE_DO_SAMPLE      # disable if sampling not desired 
CFLAGS_crbcemod.o  += -DRBCE_DO_DELAY	    # disable if delay info not desired
