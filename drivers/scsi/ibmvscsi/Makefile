EXTRA_CFLAGS += -Idrivers/scsi

obj-$(CONFIG_SCSI_IBMVSCSI)	+= ibmvscsic.o

ifeq ($(CONFIG_PPC_ISERIES),y)
  ibmvscsic-objs	:= ibmvscsi.o iseries_vscsi.o
else
  ibmvscsic-objs	:= ibmvscsi.o rpa_vscsi.o
endif
 

