#
# Makefile for the kernel pcmcia subsystem (c/o David Hinds)
#

export-objs := ds.o cs.o yenta.o

obj-$(CONFIG_PCMCIA)			+= pcmcia_core.o ds.o
ifeq ($(CONFIG_CARDBUS),y)
  obj-$(CONFIG_PCMCIA) 			+= yenta_socket.o
endif

obj-$(CONFIG_I82365)			+= i82365.o
obj-$(CONFIG_I82092)			+= i82092.o
obj-$(CONFIG_TCIC)			+= tcic.o
obj-$(CONFIG_HD64465_PCMCIA)		+= hd64465_ss.o
obj-$(CONFIG_PCMCIA_SA1100)		+= sa1100_cs.o

yenta_socket-objs				:= pci_socket.o yenta.o

pcmcia_core-objs-y				:= cistpl.o rsrc_mgr.o bulkmem.o cs.o
pcmcia_core-objs-$(CONFIG_CARDBUS)		+= cardbus.o
pcmcia_core-objs				:= $(pcmcia_core-objs-y)

sa1100_cs-objs-y				:= sa1100_generic.o
sa1100_cs-objs-$(CONFIG_SA1100_ADSBITSY)	+= sa1100_adsbitsy.o sa1111_generic.o
sa1100_cs-objs-$(CONFIG_SA1100_ASSABET)		+= sa1100_assabet.o
sa1100_cs-objs-$(CONFIG_ASSABET_NEPONSET)	+= sa1100_neponset.o sa1111_generic.o
sa1100_cs-objs-$(CONFIG_SA1100_BADGE4)		+= sa1100_badge4.o sa1111_generic.o
sa1100_cs-objs-$(CONFIG_SA1100_CERF)		+= sa1100_cerf.o
sa1100_cs-objs-$(CONFIG_SA1100_FLEXANET)	+= sa1100_flexanet.o
sa1100_cs-objs-$(CONFIG_SA1100_FREEBIRD)	+= sa1100_freebird.o
sa1100_cs-objs-$(CONFIG_SA1100_GRAPHICSMASTER)	+= sa1100_graphicsmaster.o sa1111_generic.o
sa1100_cs-objs-$(CONFIG_SA1100_GRAPHICSCLIENT)	+= sa1100_graphicsclient.o
sa1100_cs-objs-$(CONFIG_SA1100_H3600)		+= sa1100_h3600.o
sa1100_cs-objs-$(CONFIG_SA1100_JORNADA720)	+= sa1100_jornada720.o sa1111_generic.o
sa1100_cs-objs-$(CONFIG_SA1100_PANGOLIN)	+= sa1100_pangolin.o
sa1100_cs-objs-$(CONFIG_SA1100_PFS168)		+= sa1100_pfs168.o sa1111_generic.o
sa1100_cs-objs-$(CONFIG_SA1100_PT_SYSTEM3)	+= sa1100_system3.o sa1111_generic.o
sa1100_cs-objs-$(CONFIG_SA1100_SHANNON)		+= sa1100_shannon.o
sa1100_cs-objs-$(CONFIG_SA1100_SIMPAD)		+= sa1100_simpad.o
sa1100_cs-objs-$(CONFIG_SA1100_STORK)		+= sa1100_stork.o
sa1100_cs-objs-$(CONFIG_SA1100_TRIZEPS) 	+= sa1100_trizeps.o
sa1100_cs-objs-$(CONFIG_SA1100_XP860)		+= sa1100_xp860.o sa1111_generic.o
sa1100_cs-objs-$(CONFIG_SA1100_YOPY)		+= sa1100_yopy.o
sa1100_cs-objs					:= $(sa1100_cs-objs-y)

include $(TOPDIR)/Rules.make
