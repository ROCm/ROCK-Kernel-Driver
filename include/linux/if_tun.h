/*
 *  Universal TUN/TAP device driver.
 *  Copyright (C) 1999-2000 Maxim Krasnyansky <max_mk@yahoo.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *  GNU General Public License for more details.
 *
 *  $Id: if_tun.h,v 1.1 2000/08/23 05:59:28 davem Exp $
 */

#ifndef __IF_TUN_H
#define __IF_TUN_H

/* Uncomment to enable debugging */
/* #define TUN_DEBUG 1 */

#ifdef __KERNEL__

#ifdef TUN_DEBUG
#define DBG  if(tun->debug)printk
#define DBG1 if(debug==2)printk
#else
#define DBG( a... )
#define DBG1( a... )
#endif

struct tun_struct {
	char 			name[8];
	unsigned long 		flags;

	struct fasync_struct    *fasync;
	wait_queue_head_t	read_wait;

	struct net_device	dev;
	struct sk_buff_head	txq;
        struct net_device_stats	stats;

#ifdef TUN_DEBUG	
	int debug;
#endif  
};

#ifndef MIN
#define MIN(a,b) ( (a)<(b) ? (a):(b) ) 
#endif

#endif /* __KERNEL__ */

/* Number of devices */
#define TUN_MAX_DEV	255

/* TX queue size */
#define TUN_TXQ_SIZE	10

/* Max frame size */
#define TUN_MAX_FRAME	4096

/* TUN device flags */
#define TUN_TUN_DEV 	0x0001	
#define TUN_TAP_DEV	0x0002
#define TUN_TYPE_MASK   0x000f

#define TUN_FASYNC	0x0010
#define TUN_NOCHECKSUM	0x0020
#define TUN_NO_PI	0x0040

#define TUN_IFF_SET	0x1000

/* Ioctl defines */
#define TUNSETNOCSUM (('T'<< 8) | 200) 
#define TUNSETDEBUG  (('T'<< 8) | 201) 
#define TUNSETIFF    (('T'<< 8) | 202) 

/* TUNSETIFF ifr flags */
#define IFF_TUN		0x0001
#define IFF_TAP		0x0002
#define IFF_NO_PI	0x1000

struct tun_pi {
   unsigned short flags;
   unsigned short proto;
};
#define TUN_PKT_STRIP	0x0001

#endif /* __IF_TUN_H */
