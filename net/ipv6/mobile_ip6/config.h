/*
 * Configuration parameters
 *
 * $Id: s.config.h 1.11 03/09/30 12:22:53+03:00 henkku@mart10.hut.mediapoli.com $
 */

#define MIPV6VERSION "D24"
#define MIPLVERSION "v1.0"

#define CAP_CN	0x01
#define CAP_HA	0x02
#define CAP_MN	0x04

struct mip6_conf {
	int capabilities;
	int debug_level;
	int accept_ret_rout;
	int max_rtr_reachable_time;
	int eager_cell_switching;
	int max_num_tunnels;
	int min_num_tunnels;
	int binding_refresh_advice;
	int bu_lladdr;
	int bu_keymgm;
	int bu_cn_ack;
};

extern struct mip6_conf mip6node_cnf;

struct mipv6_bce;

struct mip6_func {
	void (*bce_home_add) (int ifindex, struct in6_addr *saddr, struct in6_addr *daddr, 
			      struct in6_addr *haddr, struct in6_addr *coa, __u32 lifetime, 
			      __u16 sequence, __u8 flags, __u8 *k_bu);
	void (*bce_cache_add) (int ifindex, struct in6_addr *saddr, struct in6_addr *daddr,
			       struct in6_addr *haddr, struct in6_addr *coa, __u32 lifetime,
			       __u16 sequence, __u8 flags, __u8 *k_bu);
	void (*bce_home_del) (struct in6_addr *daddr, struct in6_addr *haddr, 
			      struct in6_addr *coa, __u16 sequence, __u8 flags,
			      __u8 *k_bu);
	void (*bce_cache_del) (struct in6_addr *daddr, struct in6_addr *haddr, 
			       struct in6_addr *coa, __u16 sequence, __u8 flags,
			       __u8 *k_bu);

	void (*proxy_del) (struct in6_addr *home_addr, struct mipv6_bce *entry);
	int (*proxy_create) (int flags, int ifindex, struct in6_addr *coa,
			     struct in6_addr *our_addr, struct in6_addr *home_addr);

	int (*icmpv6_dhaad_rep_rcv) (struct sk_buff *skb);
	int (*icmpv6_dhaad_req_rcv) (struct sk_buff *skb);
	int (*icmpv6_pfxadv_rcv) (struct sk_buff *skb);
	int (*icmpv6_pfxsol_rcv) (struct sk_buff *skb);
	int (*icmpv6_paramprob_rcv) (struct sk_buff *skb);

	int (*mn_use_hao) (struct in6_addr *daddr, struct in6_addr *saddr);
	void (*mn_check_tunneled_packet) (struct sk_buff *skb);
};

extern struct mip6_func mip6_fn;
