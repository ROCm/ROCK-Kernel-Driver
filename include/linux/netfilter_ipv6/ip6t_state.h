/*
 * Copyright (C)2003 USAGI/WIDE Project
 *
 * Authors:
 *	Yasuyuki Kozakai	<yasuyuki.kozakai@toshiba.co.jp>
 *
 * Based on: include/linux/netfilter_ipv4/ipt_state.h
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */
#ifndef _IP6T_STATE_H
#define _IP6T_STATE_H

#define IP6T_STATE_BIT(ctinfo) (1 << ((ctinfo)%IP6_CT_IS_REPLY+1))
#define IP6T_STATE_INVALID (1 << 0)

struct ip6t_state_info
{
	unsigned int statemask;
};
#endif /*_IP6T_STATE_H*/
