/*
 *
 *	Generic internet FLOW.
 *
 */

#ifndef _NET_FLOW_H
#define _NET_FLOW_H

struct flowi {
	int	oif;
	int	iif;

	union {
		struct {
			__u32			daddr;
			__u32			saddr;
			__u32			fwmark;
			__u8			tos;
			__u8			scope;
		} ip4_u;
		
		struct {
			struct in6_addr *	daddr;
			struct in6_addr *	saddr;
			__u32			flowlabel;
		} ip6_u;
	} nl_u;
#define fl6_dst		nl_u.ip6_u.daddr
#define fl6_src		nl_u.ip6_u.saddr
#define fl6_flowlabel	nl_u.ip6_u.flowlabel
#define fl4_dst		nl_u.ip4_u.daddr
#define fl4_src		nl_u.ip4_u.saddr
#define fl4_fwmark	nl_u.ip4_u.fwmark
#define fl4_tos		nl_u.ip4_u.tos
#define fl4_scope	nl_u.ip4_u.scope

	__u8	proto;
	__u8	flags;
	union {
		struct {
			__u16	sport;
			__u16	dport;
		} ports;

		struct {
			__u8	type;
			__u8	code;
		} icmpt;

		__u32		spi;
	} uli_u;
#define fl_ip_sport	uli_u.ports.sport
#define fl_ip_dport	uli_u.ports.dport
#define fl_icmp_type	uli_u.icmpt.type
#define fl_icmp_code	uli_u.icmpt.code
#define fl_ipsec_spi	uli_u.spi
};
#endif
