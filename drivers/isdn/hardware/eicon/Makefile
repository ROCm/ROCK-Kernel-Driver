# Makefile for the Eicon DIVA ISDN drivers.

# Objects that export symbols.

export-objs     := diva_didd.o

# Multipart objects. 

divas-objs      := divasmain.o divasfunc.o di.o io.o istream.o diva.o dlist.o divasproc.o diva_dma.o
divacapi-objs   := capimain.o capifunc.o message.o capidtmf.o
divadidd-objs   := diva_didd.o diddfunc.o dadapter.o
diva_mnt-objs   := divamnt.o mntfunc.o debug.o maintidi.o
diva_idi-objs   := divasi.o idifunc.o um_idi.o dqueue.o dlist.o

# Optional parts of multipart objects.

divas-objs-$(CONFIG_ISDN_DIVAS_BRIPCI)  += os_bri.o  s_bri.o
divas-objs-$(CONFIG_ISDN_DIVAS_4BRIPCI) += os_4bri.o s_4bri.o
divas-objs-$(CONFIG_ISDN_DIVAS_PRIPCI)  += os_pri.o  s_pri.o

divas-objs += $(sort $(divas-objs-y))

# Each configuration option enables a list of files.

obj-$(CONFIG_ISDN_DIVAS)               += divadidd.o divas.o
obj-$(CONFIG_ISDN_DIVAS_MAINT)         += diva_mnt.o
obj-$(CONFIG_ISDN_DIVAS_USERIDI)       += diva_idi.o
obj-$(CONFIG_ISDN_DIVAS_DIVACAPI)      += divacapi.o
