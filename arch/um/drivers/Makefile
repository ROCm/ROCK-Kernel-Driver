# 
# Copyright (C) 2000, 2002 Jeff Dike (jdike@karaya.com)
# Licensed under the GPL
#

CHAN_OBJS := chan_kern.o chan_user.o line.o 

# This nonsense is due to kbuild.  In the 2.4 build, I stick -lpcap in 
# pcap-objs, and that is just included in the link command.  The 2.5 kbuild
# filters out everything from pcap-objs which are not in the built-in.o
# dependencies, which are $(obj-y).  So, -lpcap must be in $(obj-y), too.
# However, make magically expands -lfoo prerequisites into /usr/lib/libfoo.a
# file names.  This causes the kbuild filtering to filter the -lpcap from
# pcap-objs, causing the link to fail.
# So, what this does is figure out by hand (crudely) what file -lpcap really 
# is and just use it.

PCAP = $(shell for f in echo {/lib,/usr/lib}/libpcap.{a,so}; do \
       [ -f $$f ] && echo $$f ; done | head -1)

slip-objs := slip_kern.o slip_user.o
daemon-objs := daemon_kern.o daemon_user.o
mcast-objs := mcast_kern.o mcast_user.o
pcap-objs := pcap_kern.o pcap_user.o $(PCAP)
net-objs := net_kern.o net_user.o
mconsole-objs := mconsole_kern.o mconsole_user.o
hostaudio-objs := hostaudio_kern.o hostaudio_user.o
ubd-objs := ubd_kern.o ubd_user.o
port-objs := port_kern.o port_user.o
harddog-objs := harddog_kern.o harddog_user.o

export-objs := mconsole_kern.o

obj-y = 
obj-$(CONFIG_SSL) += ssl.o 
obj-$(CONFIG_UML_NET_SLIP) += slip.o
obj-$(CONFIG_UML_NET_DAEMON) += daemon.o 
obj-$(CONFIG_UML_NET_MCAST) += mcast.o 
obj-$(CONFIG_UML_NET_PCAP) += pcap.o $(PCAP)
obj-$(CONFIG_UML_NET) += net.o 
obj-$(CONFIG_MCONSOLE) += mconsole.o
obj-$(CONFIG_MMAPPER) += mmapper_kern.o 
obj-$(CONFIG_BLK_DEV_UBD) += ubd.o 
obj-$(CONFIG_HOSTAUDIO) += hostaudio.o
obj-$(CONFIG_FD_CHAN) += fd.o 
obj-$(CONFIG_NULL_CHAN) += null.o 
obj-$(CONFIG_PORT_CHAN) += port.o
obj-$(CONFIG_PTY_CHAN) += pty.o
obj-$(CONFIG_TTY_CHAN) += tty.o 
obj-$(CONFIG_XTERM_CHAN) += xterm.o
obj-$(CONFIG_UML_WATCHDOG) += harddog.o

CFLAGS_pcap_user.o = -I/usr/include/pcap

obj-y += stdio_console.o $(CHAN_OBJS)

USER_SINGLE_OBJS = $(foreach f,$(patsubst %.o,%,$(obj-y) $(obj-m)),$($(f)-objs))

USER_OBJS = $(filter %_user.o,$(obj-y) $(obj-m) $(USER_SINGLE_OBJS)) fd.o \
	null.o pty.o tty.o xterm.o

include $(TOPDIR)/Rules.make

$(USER_OBJS) : %.o: %.c
	$(CC) $(CFLAGS_$@) $(USER_CFLAGS) -c -o $@ $<

clean:

modules:

fastdep:

dep:

archmrproper: clean

