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

int xfrm_check_output(struct xfrm_state *x,
                      struct sk_buff *skb, unsigned short family)
{
	int err;
	
	err = xfrm_state_check_expire(x);
	if (err)
		goto out;
		
	if (x->props.mode) {
		switch (family) {
		case AF_INET:
			err = xfrm4_tunnel_check_size(skb);
			break;
			
		case AF_INET6:
			err = xfrm6_tunnel_check_size(skb);
			break;
			
		default:
			err = -EINVAL;
		}
		
		if (err)
			goto out;
	}

	err = xfrm_state_check_space(x, skb);
out:
	return err;
}
