
EXTRA_CFLAGS	+= -I$(TOPDIR)/drivers/scsi

obj-$(CONFIG_SCSI_AACRAID) := aacraid.o

aacraid-objs	:= linit.o aachba.o commctrl.o comminit.o commsup.o \
		   dpcsup.o rx.o sa.o

include $(TOPDIR)/Rules.make
