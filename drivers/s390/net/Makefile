all: s390-net.o

CFLAGS += 
O_TARGET := s390-net.o

obj-y    := iucv.o

ifeq ($(CONFIG_CTC),y)
  obj-y += ctcmain.o fsm.o
else
  ifeq ($(CONFIG_CTC),m)
  obj-m += ctc.o
ctc.o: ctcmain.o fsm.o
	$(LD) -r ctcmain.o fsm.o -o $@
  endif
endif

obj-$(CONFIG_IUCV) += iucv.o netiucv.o

include $(TOPDIR)/Rules.make
