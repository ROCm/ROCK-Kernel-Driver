#
# drivers/scsi/pcmcia/Makefile
#
# Makefile for the Linux PCMCIA SCSI drivers.
#

obj-y		:=
obj-m		:=
obj-n		:=
obj-		:=

vpath %c ..

CFLAGS_aha152x.o = -DPCMCIA -D__NO_VERSION__ -DAHA152X_STAT
CFLAGS_fdomain.o = -DPCMCIA -D__NO_VERSION__
CFLAGS_qlogicfas.o = -DPCMCIA -D__NO_VERSION__

# 16-bit client drivers
obj-$(CONFIG_PCMCIA_QLOGIC)	+= qlogic_cs.o
obj-$(CONFIG_PCMCIA_FDOMAIN)	+= fdomain_cs.o
obj-$(CONFIG_PCMCIA_AHA152X)	+= aha152x_cs.o

list-multi	:= qlogic_cs.o fdomain_cs.o aha152x_cs.o
aha152x-objs	:= aha152x_stub.o aha152x.o
fdomain-objs	:= fdomain_stub.o fdomain.o
qlogic-objs	:= qlogic_stub.o qlogicfas.o

include $(TOPDIR)/Rules.make

aha152x_cs.o: $(aha152x-objs)
	$(LD) -r -o $@ $(aha152x-objs)

fdomain_cs.o: $(fdomain-objs)
	$(LD) -r -o $@ $(fdomain-objs)

qlogic_cs.o: $(qlogic-objs)
	$(LD) -r -o $@ $(qlogic-objs)
