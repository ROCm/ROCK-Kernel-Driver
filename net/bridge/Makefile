#
# Makefile for the IEEE 802.1d ethernet bridging layer.
#

obj-$(CONFIG_BRIDGE) += bridge.o

bridge-y	:= br.o br_device.o br_fdb.o br_forward.o br_if.o br_input.o \
			br_ioctl.o br_notify.o br_stp.o br_stp_bpdu.o \
			br_stp_if.o br_stp_timer.o

# br_netfilter only deals with IPv4 and ARP filtering, both are INET protocols
ifeq ($(CONFIG_INET),y)
bridge-$(CONFIG_NETFILTER)	+= br_netfilter.o
endif

obj-$(CONFIG_BRIDGE_NF_EBTABLES) += netfilter/
