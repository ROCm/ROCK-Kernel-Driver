/*
 * Copyright (C)2003 USAGI/WIDE Project
 *
 * Authors:
 *	Yasuyuki Kozakai	<yasuyuki.kozakai@toshiba.co.jp>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */
#ifndef _IP6_CONNTRACK_REASM_H
#define _IP6_CONNTRACK_REASM_H

#include <linux/netfilter.h>
extern struct sk_buff *
ip6_ct_gather_frags(struct sk_buff *skb);

extern int
ip6_ct_output_frags(struct sk_buff *skb, struct nf_info *info);

extern int ip6_ct_kfree_frags(struct sk_buff *skb);

extern int ip6_ct_frags_init(void);
extern void ip6_ct_frags_cleanup(void);

#endif /* _IP6_CONNTRACK_REASM_H */

