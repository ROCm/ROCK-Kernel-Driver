#ifndef _INET_ECN_H_
#define _INET_ECN_H_

#include <linux/ip.h>

enum {
	INET_ECN_NOT_ECT = 0,
	INET_ECN_ECT_1 = 1,
	INET_ECN_ECT_0 = 2,
	INET_ECN_CE = 3,
	INET_ECN_MASK = 3,
};

static inline int INET_ECN_is_ce(__u8 dsfield)
{
	return (dsfield & INET_ECN_MASK) == INET_ECN_CE;
}

static inline int INET_ECN_is_not_ce(__u8 dsfield)
{
	return (dsfield & INET_ECN_MASK) == INET_ECN_ECT_0;
}

static inline int INET_ECN_is_capable(__u8 dsfield)
{
	return (dsfield & INET_ECN_ECT_0);
}

static inline __u8 INET_ECN_encapsulate(__u8 outer, __u8 inner)
{
	outer &= ~INET_ECN_MASK;
	if (INET_ECN_is_capable(inner))
		outer |= (inner & INET_ECN_MASK);
	return outer;
}

#define	INET_ECN_xmit(sk) do { inet_sk(sk)->tos |= INET_ECN_ECT_0; } while (0)
#define	INET_ECN_dontxmit(sk) \
	do { inet_sk(sk)->tos &= ~INET_ECN_MASK; } while (0)

#define IP6_ECN_flow_init(label) do {		\
      (label) &= ~htonl(INET_ECN_MASK << 20);	\
    } while (0)

#define	IP6_ECN_flow_xmit(sk, label) do {				\
	if (INET_ECN_is_capable(inet_sk(sk)->tos))			\
		(label) |= __constant_htons(INET_ECN_ECT_0 << 4);	\
    } while (0)

static inline void IP_ECN_set_ce(struct iphdr *iph)
{
	u32 check = iph->check;
	check += __constant_htons(0xFFFE);
	iph->check = check + (check>=0xFFFF);
	iph->tos |= INET_ECN_CE;
}

static inline void IP_ECN_clear(struct iphdr *iph)
{
	iph->tos &= ~INET_ECN_MASK;
}

struct ipv6hdr;

static inline void IP6_ECN_set_ce(struct ipv6hdr *iph)
{
	*(u32*)iph |= htonl(INET_ECN_CE << 20);
}

static inline void IP6_ECN_clear(struct ipv6hdr *iph)
{
	*(u32*)iph &= ~htonl(INET_ECN_MASK << 20);
}

#define ip6_get_dsfield(iph) ((ntohs(*(u16*)(iph)) >> 4) & 0xFF)

#endif
