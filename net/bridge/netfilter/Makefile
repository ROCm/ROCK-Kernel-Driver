#
# Makefile for the netfilter modules for Link Layer filtering on a bridge.
#

export-objs := ebtables.o

obj-$(CONFIG_BRIDGE_NF_EBTABLES) += ebtables.o
obj-$(CONFIG_BRIDGE_EBT_T_FILTER) += ebtable_filter.o
obj-$(CONFIG_BRIDGE_EBT_T_NAT) += ebtable_nat.o
obj-$(CONFIG_BRIDGE_EBT_BROUTE) += ebtable_broute.o
obj-$(CONFIG_BRIDGE_EBT_IPF) += ebt_ip.o
obj-$(CONFIG_BRIDGE_EBT_ARPF) += ebt_arp.o
obj-$(CONFIG_BRIDGE_EBT_VLANF) += ebt_vlan.o
obj-$(CONFIG_BRIDGE_EBT_MARKF) += ebt_mark_m.o
obj-$(CONFIG_BRIDGE_EBT_LOG) += ebt_log.o
obj-$(CONFIG_BRIDGE_EBT_SNAT) += ebt_snat.o
obj-$(CONFIG_BRIDGE_EBT_DNAT) += ebt_dnat.o
obj-$(CONFIG_BRIDGE_EBT_REDIRECT) += ebt_redirect.o
obj-$(CONFIG_BRIDGE_EBT_MARK_T) += ebt_mark.o
include $(TOPDIR)/Rules.make
