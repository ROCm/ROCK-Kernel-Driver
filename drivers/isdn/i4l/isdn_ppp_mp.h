
#ifndef __ISDN_PPP_MP_H__
#define __ISDN_PPP_MP_H__

#include <linux/kernel.h>
#include <linux/isdn.h>


#ifdef CONFIG_ISDN_MPP

int  ippp_mp_bind(isdn_net_dev *idev);
void ippp_mp_disconnected(isdn_net_dev *idev);
int  ippp_mp_bundle(isdn_net_dev *idev, int val);
void ippp_mp_xmit(isdn_net_dev *idev, struct sk_buff *skb, u16 proto);
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
ippp_mp_xmit(isdn_net_dev *idev, struct sk_buff *skb, u16 proto)
{
	ippp_xmit(idev, skb, proto);
}

static inline void 
ippp_mp_receive(isdn_net_dev *idev, struct sk_buff *skb, u16 proto)
{
	ippp_receive(idev, skb, proto);
}

#endif

#endif
