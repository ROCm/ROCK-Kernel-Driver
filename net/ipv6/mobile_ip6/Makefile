#
# Makefile for the MIPL Mobile IPv6 for Linux.
#

mip6_base-objs := hashlist.o bcache.o mobhdr_common.o stats.o exthdrs.o \
	rr_crypto.o hmac.o auth_opt.o mipv6_icmp.o module_cn.o \
	prefix.o sortedlist.o

mip6_ha-objs := halist.o mipv6_icmp_ha.o tunnel_ha.o \
		ndisc_ha.o ha.o module_ha.o prefix_ha.o

mip6_mn-objs := mipv6_icmp_mn.o ioctl_mn.o tunnel_mn.o \
		mdetect.o bul.o multiaccess_ctl.o mobhdr_mn.o mn.o \
		module_mn.o 

obj-$(CONFIG_IPV6_MOBILITY) += mip6_base.o
obj-$(CONFIG_IPV6_MOBILITY_HA) += mip6_ha.o
obj-$(CONFIG_IPV6_MOBILITY_MN) += mip6_mn.o
