# 
# Copyright (C) 2000, 2002 Jeff Dike (jdike@karaya.com)
# Licensed under the GPL
#

O_TARGET := drivers.o

list-multi := tuntap.o ethertap.o

ethertap-objs := ethertap_kern.o ethertap_user.o
tuntap-objs := tuntap_kern.o tuntap_user.o

obj-y = 
obj-$(CONFIG_UML_NET_ETHERTAP) += ethertap.o
obj-$(CONFIG_UML_NET_TUNTAP) += tuntap.o

USER_SINGLE_OBJS = $(foreach f,$(patsubst %.o,%,$(obj-y)),$($(f)-objs))

USER_OBJS = $(filter %_user.o,$(obj-y) $(USER_SINGLE_OBJS))

include $(TOPDIR)/Rules.make

$(USER_OBJS) : %.o: %.c
	$(CC) $(CFLAGS_$@) $(USER_CFLAGS) -c -o $@ $<

ethertap.o : $(ethertap-objs)

tuntap.o : $(tuntap-objs)

$(list-multi) : # This doesn't work, but should : '%.o : $(%-objs)'
	$(LD) $(LD_RFLAG) -r -o $@ $($(patsubst %.o,%,$@)-objs)
