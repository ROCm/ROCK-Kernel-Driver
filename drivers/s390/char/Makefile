all: s390-char.o

CFLAFS += 
O_TARGET := s390-char.o

obj-$(CONFIG_3215_CONSOLE) += con3215.o

obj-$(CONFIG_HWC) += hwc_con.o hwc_rw.o hwc_tty.o

# stuff for building tape390.o
T390_OBJS = tape.o
ifeq ($(CONFIG_S390_TAPE_CHAR),y)
  T390_OBJS += tapechar.o
endif
ifeq ($(CONFIG_S390_TAPE_BLOCK),y)
  T390_OBJS += tapeblock.o
endif
ifeq ($(CONFIG_S390_TAPE_3480),y)
  T390_OBJS += tape3480.o
  CONFIG_S390_TAPE_NEED_34xx = y 
endif
ifeq ($(CONFIG_S390_TAPE_3490),y)
  T390_OBJS += tape3490.o
  CONFIG_S390_TAPE_NEED_34xx = y
endif
ifeq ($(CONFIG_S390_TAPE_NEED_34xx),y) 
  T390_OBJS += tape34xx.o
endif
obj-$(CONFIG_S390_TAPE) += tape390.o

tape390.o: $(T390_OBJS)
	$(LD) -r -o $@ $(T390_OBJS)
include $(TOPDIR)/Rules.make
