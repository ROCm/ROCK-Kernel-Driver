all: s390-char.o

CFLAFS += 
O_TARGET := s390-char.o
O_OBJS    :=         
M_OBJS   :=

ifeq ($(CONFIG_3215_CONSOLE),y)
  O_OBJS += con3215.o
endif

ifeq ($(CONFIG_HWC),y)
  O_OBJS += hwc_con.o hwc_rw.o hwc_tty.o
endif

include $(TOPDIR)/Rules.make
