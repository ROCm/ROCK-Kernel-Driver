/* Linux ISDN subsystem, PPP VJ header compression
 *
 * Copyright 1995,96    by Michael Hipp (Michael.Hipp@student.uni-tuebingen.de)
 *           1999-2002  by Kai Germaschewski <kai@germaschewski.name>
 *
 * This software may be used and distributed according to the terms
 * of the GNU General Public License, incorporated herein by reference.
 */

#ifndef __ISDN_PPP_VJ_H__
#define __ISDN_PPP_VJ_H__

#include "isdn_net_lib.h"

#ifdef CONFIG_ISDN_PPP_VJ


struct slcompress *
ippp_vj_alloc(void);

void
ippp_vj_free(struct slcompress *slcomp);

int
ippp_vj_set_maxcid(isdn_net_dev *idev, int val);

void
ippp_vj_decompress(isdn_net_dev *idev, struct sk_buff *skb_old, u16 proto);

struct sk_buff *
ippp_vj_compress(isdn_net_dev *idev, struct sk_buff *skb_old, u16 *proto);


#else


static inline struct slcompress *
ippp_vj_alloc(void)
{ return (struct slcompress *) !NULL; }

static inline void
ippp_vj_free(struct slcompress *slcomp) 
{ }

static inline int
ippp_vj_set_maxcid(isdn_net_dev *idev, int val)
{ return -EINVAL; }

static inline struct sk_buff *
ippp_vj_decompress(struct slcompress *slcomp, struct sk_buff *skb_old, 
		   u16 proto)
{ return skb_old; }

static inline struct sk_buff *
ippp_vj_compress(isdn_net_dev *idev, struct sk_buff *skb_old, u16 *proto)
{ return skb_old; }


#endif

#endif
