#
# Makefile for the Linux networking core.
#

obj-y := sock.o skbuff.o iovec.o datagram.o scm.o

ifeq ($(CONFIG_SYSCTL),y)
ifeq ($(CONFIG_NET),y)
obj-y += sysctl_net_core.o
endif
endif

obj-$(CONFIG_NET) += dev.o dev_mcast.o dst.o neighbour.o rtnetlink.o utils.o link_watch.o filter.o

obj-$(CONFIG_NETFILTER) += netfilter.o
obj-$(CONFIG_NET_DIVERT) += dv.o
obj-$(CONFIG_NET_PROFILE) += profile.o
obj-$(CONFIG_NET_PKTGEN) += pktgen.o
obj-$(CONFIG_NET_RADIO) += wireless.o
