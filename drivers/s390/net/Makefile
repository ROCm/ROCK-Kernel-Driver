all: s390-net.o

CFLAFS += 
O_TARGET := s390-net.o
O_OBJS    :=         
M_OBJS   :=

ifeq ($(CONFIG_CTC),y)
  O_OBJS += ctc.o
endif

ifeq ($(CONFIG_IUCV),y)
  O_OBJS += iucv.o
endif

include $(TOPDIR)/Rules.make
