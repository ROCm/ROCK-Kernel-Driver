#
# Makefile for the kernel pcmcia subsystem (c/o David Hinds)
#
# Note! Dependencies are done automagically by 'make dep', which also
# removes any old dependencies. DON'T put your own dependencies here
# unless it's something special (ie not a .c file).
#
# Note 2! The CFLAGS definitions are now inherited from the
# parent makes..

O_TARGET := pcmcia.o

export-objs := ds.o cs.o cb_enabler.o yenta.o pci_socket.o

multi-list = pcmcia_core.o yenta_socket.o

yenta_socket-objs := pci_socket.o yenta.o
pcmcia_core-objs := cistpl.o rsrc_mgr.o bulkmem.o cs.o

ifeq ($(CONFIG_CARDBUS),y)
  pcmcia_core-objs += cardbus.o
endif

ifeq ($(CONFIG_PCMCIA),y)
  obj-y   := cistpl.o rsrc_mgr.o bulkmem.o ds.o cs.o
  ifeq ($(CONFIG_CARDBUS),y)
    obj-y += cardbus.o cb_enabler.o yenta.o pci_socket.o
  endif
  ifeq ($(CONFIG_I82365),y)
    obj-y += i82365.o
  endif
  ifeq ($(CONFIG_TCIC),y)
    obj-y += tcic.o
  endif
else
  ifeq ($(CONFIG_PCMCIA),m)
    obj-m   := pcmcia_core.o ds.o
    ifeq ($(CONFIG_I82365),y)
      obj-m += i82365.o
    endif
    ifeq ($(CONFIG_TCIC),y)
      obj-m += tcic.o
    endif
     ifeq ($(CONFIG_CARDBUS),y)
	obj-m += yenta_socket.o cb_enabler.o
     endif
  endif
endif

include $(TOPDIR)/Rules.make

pcmcia_core.o:  $(pcmcia_core-objs)
	$(LD) $(LD_RFLAG) -r -o $@ $(pcmcia_core-objs)

yenta_socket.o: $(yenta_socket-objs)
	$(LD) $(LD_RFLAG) -r -o $@ $(yenta_socket-objs)
