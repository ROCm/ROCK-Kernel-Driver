/*
 *	Generic address resultion entity
 *
 *	Authors:
 *	net_random Alan Cox
 *	net_ratelimit Andy Kleen
 *
 *	Created by Alexey Kuznetsov <kuznet@ms2.inr.ac.ru>
 *
 *	This program is free software; you can redistribute it and/or
 *      modify it under the terms of the GNU General Public License
 *      as published by the Free Software Foundation; either version
 *      2 of the License, or (at your option) any later version.
 */

#include <linux/module.h>
#include <linux/jiffies.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/string.h>
#include <linux/types.h>

#include <asm/system.h>
#include <asm/uaccess.h>

static unsigned long net_rand_seed = 152L;

unsigned long net_random(void)
{
	net_rand_seed=net_rand_seed*69069L+1;
        return net_rand_seed^jiffies;
}

void net_srandom(unsigned long entropy)
{
	net_rand_seed ^= entropy;
	net_random();
}

int net_msg_cost = 5*HZ;
int net_msg_burst = 10;

/* 
 * All net warning printk()s should be guarded by this function.
 */ 
int net_ratelimit(void)
{
	return __printk_ratelimit(net_msg_cost, net_msg_burst);
}

EXPORT_SYMBOL(net_random);
EXPORT_SYMBOL(net_ratelimit);
EXPORT_SYMBOL(net_srandom);
