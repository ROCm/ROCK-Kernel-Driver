#
# Makefile for the netfilter modules on top of IPv6.
#

export-objs := ip6_tables.o

# Link order matters here.
obj-$(CONFIG_IP6_NF_IPTABLES) += ip6_tables.o
obj-$(CONFIG_IP6_NF_MATCH_LIMIT) += ip6t_limit.o
obj-$(CONFIG_IP6_NF_MATCH_MARK) += ip6t_mark.o
obj-$(CONFIG_IP6_NF_MATCH_MAC) += ip6t_mac.o
obj-$(CONFIG_IP6_NF_MATCH_MULTIPORT) += ip6t_multiport.o
obj-$(CONFIG_IP6_NF_MATCH_OWNER) += ip6t_owner.o
obj-$(CONFIG_IP6_NF_FILTER) += ip6table_filter.o
obj-$(CONFIG_IP6_NF_MANGLE) += ip6table_mangle.o
obj-$(CONFIG_IP6_NF_TARGET_MARK) += ip6t_MARK.o
obj-$(CONFIG_IP6_NF_QUEUE) += ip6_queue.o
obj-$(CONFIG_IP6_NF_TARGET_LOG) += ip6t_LOG.o

include $(TOPDIR)/Rules.make
