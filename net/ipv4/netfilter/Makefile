#
# Makefile for the netfilter modules on top of IPv4.
#
# Note! Dependencies are done automagically by 'make dep', which also
# removes any old dependencies. DON'T put your own dependencies here
# unless it's something special (ie not a .c file).
#
# Note 2! The CFLAGS definition is now in the main makefile...

O_TARGET := netfilter.o

export-objs = ip_conntrack_standalone.o ip_conntrack_ftp.o ip_fw_compat.o ip_nat_standalone.o ip_tables.o

# Multipart objects.
list-multi		:= ip_conntrack.o iptable_nat.o ipfwadm.o ipchains.o

# objects for the conntrack and NAT core (used by standalone and backw. compat)
ip_nf_conntrack-objs	:= ip_conntrack_core.o ip_conntrack_proto_generic.o ip_conntrack_proto_tcp.o ip_conntrack_proto_udp.o ip_conntrack_proto_icmp.o
ip_nf_nat-objs		:= ip_nat_core.o ip_nat_proto_unknown.o ip_nat_proto_tcp.o ip_nat_proto_udp.o ip_nat_proto_icmp.o

# objects for the standalone - connection tracking / NAT
ip_conntrack-objs	:= ip_conntrack_standalone.o $(ip_nf_conntrack-objs)
iptable_nat-objs	:= ip_nat_standalone.o ip_nat_rule.o $(ip_nf_nat-objs)

# objects for backwards compatibility mode
ip_nf_compat-objs	:= ip_fw_compat.o ip_fw_compat_redir.o ip_fw_compat_masq.o $(ip_nf_conntrack-objs) $(ip_nf_nat-objs)

ipfwadm-objs		:= $(ip_nf_compat-objs) ipfwadm_core.o
ipchains-objs		:= $(ip_nf_compat-objs) ipchains_core.o

# connection tracking
obj-$(CONFIG_IP_NF_CONNTRACK) += ip_conntrack.o

# connection tracking helpers
obj-$(CONFIG_IP_NF_FTP) += ip_conntrack_ftp.o

# NAT helpers 
obj-$(CONFIG_IP_NF_FTP) += ip_nat_ftp.o

# generic IP tables 
obj-$(CONFIG_IP_NF_IPTABLES) += ip_tables.o

# the three instances of ip_tables
obj-$(CONFIG_IP_NF_FILTER) += iptable_filter.o
obj-$(CONFIG_IP_NF_MANGLE) += iptable_mangle.o
obj-$(CONFIG_IP_NF_NAT) += iptable_nat.o

# matches
obj-$(CONFIG_IP_NF_MATCH_LIMIT) += ipt_limit.o
obj-$(CONFIG_IP_NF_MATCH_MARK) += ipt_mark.o
obj-$(CONFIG_IP_NF_MATCH_MAC) += ipt_mac.o
obj-$(CONFIG_IP_NF_MATCH_MULTIPORT) += ipt_multiport.o
obj-$(CONFIG_IP_NF_MATCH_OWNER) += ipt_owner.o
obj-$(CONFIG_IP_NF_MATCH_TOS) += ipt_tos.o
obj-$(CONFIG_IP_NF_MATCH_STATE) += ipt_state.o
obj-$(CONFIG_IP_NF_MATCH_UNCLEAN) += ipt_unclean.o

# targets
obj-$(CONFIG_IP_NF_TARGET_REJECT) += ipt_REJECT.o
obj-$(CONFIG_IP_NF_TARGET_MIRROR) += ipt_MIRROR.o
obj-$(CONFIG_IP_NF_TARGET_TOS) += ipt_TOS.o
obj-$(CONFIG_IP_NF_TARGET_MARK) += ipt_MARK.o
obj-$(CONFIG_IP_NF_TARGET_MASQUERADE) += ipt_MASQUERADE.o
obj-$(CONFIG_IP_NF_TARGET_REDIRECT) += ipt_REDIRECT.o
obj-$(CONFIG_IP_NF_TARGET_LOG) += ipt_LOG.o

# backwards compatibility 
obj-$(CONFIG_IP_NF_COMPAT_IPCHAINS) += ipchains.o
obj-$(CONFIG_IP_NF_COMPAT_IPFWADM) += ipfwadm.o

obj-$(CONFIG_IP_NF_QUEUE) += ip_queue.o

include $(TOPDIR)/Rules.make

ip_conntrack.o: $(ip_conntrack-objs)
	$(LD) -r -o $@ $(ip_conntrack-objs)

iptable_nat.o: $(iptable_nat-objs)
	$(LD) -r -o $@ $(iptable_nat-objs)

ipfwadm.o: $(ipfwadm-objs)
	$(LD) -r -o $@ $(ipfwadm-objs)

ipchains.o: $(ipchains-objs)
	$(LD) -r -o $@ $(ipchains-objs)
