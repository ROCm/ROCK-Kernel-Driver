/* Linux ISDN subsystem, PPP CCP support
 *
 * Copyright 1994-1998  by Fritz Elfert (fritz@isdn4linux.de)
 *           1995,96    by Thinking Objects Software GmbH Wuerzburg
 *           1995,96    by Michael Hipp (Michael.Hipp@student.uni-tuebingen.de)
 *           1999-2002  by Kai Germaschewski <kai@germaschewski.name>
 *
 * This software may be used and distributed according to the terms
 * of the GNU General Public License, incorporated herein by reference.
 */

#ifndef __ISDN_PPP_MP_H__
#define __ISDN_PPP_MP_H__

#include "isdn_net_lib.h"

#ifdef CONFIG_ISDN_MPP

int  ippp_mp_bind(isdn_net_dev *idev);
void ippp_mp_disconnected(isdn_net_dev *idev);
int  ippp_mp_bundle(isdn_net_dev *idev, int val);
void ippp_mp_xmit(isdn_net_dev *idev, struct sk_buff *skb);
void ippp_mp_receive(isdn_net_dev *idev, struct sk_buff *skb, u16 proto);

#else

static inline int
ippp_mp_bind(isdn_net_dev *idev)
{
	return 0;
}

static void
ippp_mp_disconnected(isdn_net_dev *idev)
{
}

static inline int
ippp_mp_bundle(isdn_net_dev *idev, int val)
{
	return -EINVAL;
}

static inline void
ippp_mp_xmit(isdn_net_dev *idev, struct sk_buff *skb)
{
	ippp_xmit(idev, skb);
}

static inline void 
ippp_mp_receive(isdn_net_dev *idev, struct sk_buff *skb, u16 proto)
{
	ippp_receive(idev, skb, proto);
}

#endif

#endif
