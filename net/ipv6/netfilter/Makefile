#
# Makefile for the netfilter modules on top of IPv6.
#

# objects for the conntrack
ip6_nf_conntrack-objs	:= ip6_conntrack_core.o ip6_conntrack_proto_generic.o ip6_conntrack_proto_tcp.o ip6_conntrack_proto_udp.o ip6_conntrack_proto_icmpv6.o ip6_conntrack_reasm.o

# objects for the standalone - connection tracking
ip6_conntrack-objs	:= ip6_conntrack_standalone.o $(ip6_nf_conntrack-objs)

# connection tracking
obj-$(CONFIG_IP6_NF_CONNTRACK) += ip6_conntrack.o

# connection tracking helpers
obj-$(CONFIG_IP6_NF_FTP) += ip6_conntrack_ftp.o

# Link order matters here.
obj-$(CONFIG_IP6_NF_IPTABLES) += ip6_tables.o
obj-$(CONFIG_IP6_NF_MATCH_LIMIT) += ip6t_limit.o
obj-$(CONFIG_IP6_NF_MATCH_MARK) += ip6t_mark.o
obj-$(CONFIG_IP6_NF_MATCH_LENGTH) += ip6t_length.o
obj-$(CONFIG_IP6_NF_MATCH_MAC) += ip6t_mac.o
obj-$(CONFIG_IP6_NF_MATCH_RT) += ip6t_rt.o
obj-$(CONFIG_IP6_NF_MATCH_OPTS) += ip6t_hbh.o ip6t_dst.o
obj-$(CONFIG_IP6_NF_MATCH_IPV6HEADER) += ip6t_ipv6header.o
obj-$(CONFIG_IP6_NF_MATCH_FRAG) += ip6t_frag.o
obj-$(CONFIG_IP6_NF_MATCH_AHESP) += ip6t_esp.o ip6t_ah.o
obj-$(CONFIG_IP6_NF_MATCH_EUI64) += ip6t_eui64.o
obj-$(CONFIG_IP6_NF_MATCH_MULTIPORT) += ip6t_multiport.o
obj-$(CONFIG_IP6_NF_MATCH_OWNER) += ip6t_owner.o
obj-$(CONFIG_IP6_NF_FILTER) += ip6table_filter.o
obj-$(CONFIG_IP6_NF_MANGLE) += ip6table_mangle.o
obj-$(CONFIG_IP6_NF_TARGET_MARK) += ip6t_MARK.o
obj-$(CONFIG_IP6_NF_QUEUE) += ip6_queue.o
obj-$(CONFIG_IP6_NF_TARGET_LOG) += ip6t_LOG.o
obj-$(CONFIG_IP6_NF_RAW) += ip6table_raw.o
obj-$(CONFIG_IP6_NF_MATCH_HL) += ip6t_hl.o
obj-$(CONFIG_IP6_NF_MATCH_STATE) += ip6t_state.o
obj-$(CONFIG_IP6_NF_TARGET_REJECT) += ip6t_REJECT.o
