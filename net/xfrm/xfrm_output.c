/* 
 * generic xfrm output routines
 *
 * Copyright (c) 2003 James Morris <jmorris@intercode.com.au>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option) 
 * any later version.
 */
#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/skbuff.h>
#include <net/xfrm.h>

static int xfrm_tunnel_check_size(struct sk_buff *skb, unsigned short family)
{
	struct xfrm_state_afinfo *afinfo;
	int err;

	afinfo = xfrm_state_get_afinfo(family);
	if (unlikely(afinfo == NULL))
		return -EAFNOSUPPORT;

	err = afinfo->tunnel_check_size(skb);
	xfrm_state_put_afinfo(afinfo);

	return err;
}

int xfrm_check_output(struct xfrm_state *x,
                      struct sk_buff *skb, unsigned short family)
{
	int err;
	
	err = xfrm_state_check_expire(x);
	if (err)
		goto out;
		
	if (x->props.mode) {
		err = xfrm_tunnel_check_size(skb, family);
		if (err)
			goto out;
	}

	err = xfrm_state_check_space(x, skb);
out:
	return err;
}
