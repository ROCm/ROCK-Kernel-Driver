all: s390-misc.o

CFLAFS += 
O_TARGET := s390-misc.o
O_OBJS    :=         
M_OBJS   := 

ifeq ($(CONFIG_CHANDEV),y)
  O_OBJS += chandev.o
endif

include $(TOPDIR)/Rules.make

