O_TARGET        := pci.o

obj-y   := dma.o i386.o

ifdef CONFIG_VISWS
obj-y   += visws.o
else

obj-y   += irq.o

obj-y   += common.o 
endif

export-objs     +=      $(obj-y)

include $(TOPDIR)/Rules.make
