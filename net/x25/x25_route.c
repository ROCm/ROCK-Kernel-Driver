/*
 *	X.25 Packet Layer release 002
 *
 *	This is ALPHA test software. This code may break your machine,
 *	randomly fail to work with new releases, misbehave and/or generally
 *	screw up. It might even work. 
 *
 *	This code REQUIRES 2.1.15 or higher
 *
 *	This module:
 *		This module is free software; you can redistribute it and/or
 *		modify it under the terms of the GNU General Public License
 *		as published by the Free Software Foundation; either version
 *		2 of the License, or (at your option) any later version.
 *
 *	History
 *	X.25 001	Jonathan Naylor	Started coding.
 */

#include <linux/config.h>
#include <linux/errno.h>
#include <linux/types.h>
#include <linux/socket.h>
#include <linux/in.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/timer.h>
#include <linux/string.h>
#include <linux/sockios.h>
#include <linux/net.h>
#include <linux/inet.h>
#include <linux/netdevice.h>
#include <net/arp.h>
#include <linux/if_arp.h>
#include <linux/skbuff.h>
#include <net/sock.h>
#include <asm/system.h>
#include <asm/uaccess.h>
#include <linux/fcntl.h>
#include <linux/termios.h>	/* For TIOCINQ/OUTQ */
#include <linux/mm.h>
#include <linux/interrupt.h>
#include <linux/notifier.h>
#include <linux/init.h>
#include <net/x25.h>

static struct x25_route *x25_route_list; /* = NULL initially */

/*
 *	Add a new route.
 */
static int x25_add_route(struct x25_address *address, unsigned int sigdigits,
			 struct net_device *dev)
{
	struct x25_route *x25_route;
	unsigned long flags;
	int rc = -EINVAL;

	for (x25_route = x25_route_list; x25_route; x25_route = x25_route->next)
		if (!memcmp(&x25_route->address, address, sigdigits) &&
		    x25_route->sigdigits == sigdigits)
			goto out;

	x25_route = kmalloc(sizeof(*x25_route), GFP_ATOMIC);
	rc = -ENOMEM;
	if (!x25_route)
		goto out;

	strcpy(x25_route->address.x25_addr, "000000000000000");
	memcpy(x25_route->address.x25_addr, address->x25_addr, sigdigits);

	x25_route->sigdigits = sigdigits;
	x25_route->dev       = dev;

	save_flags(flags); cli();
	x25_route->next = x25_route_list;
	x25_route_list  = x25_route;
	restore_flags(flags);
	rc = 0;
out:
	return rc;
}

static void x25_remove_route(struct x25_route *x25_route)
{
	struct x25_route *s;
	unsigned long flags;

	save_flags(flags);
	cli();

	if ((s = x25_route_list) == x25_route) {
		x25_route_list = x25_route->next;
		restore_flags(flags);
		return;
	}

	while (s && s->next) {
		if (s->next == x25_route) {
			s->next = x25_route->next;
			goto out_kfree_route;
		}

		s = s->next;
	}
out:
	restore_flags(flags);
	return;
out_kfree_route:
	kfree(x25_route);
	goto out;
}

static int x25_del_route(struct x25_address *address, unsigned int sigdigits,
			 struct net_device *dev)
{
	struct x25_route *x25_route = x25_route_list;
	int rc = -EINVAL;

	for (; x25_route; x25_route = x25_route->next)
		if (!memcmp(&x25_route->address, address, sigdigits) &&
		    x25_route->sigdigits == sigdigits &&
		    x25_route->dev == dev) {
			x25_remove_route(x25_route);
			rc = 0;
			break;
		}

	return rc;
}

/*
 *	A device has been removed, remove its routes.
 */
void x25_route_device_down(struct net_device *dev)
{
	struct x25_route *route, *x25_route = x25_route_list;

	while (x25_route) {
		route     = x25_route;
		x25_route = x25_route->next;

		if (route->dev == dev)
			x25_remove_route(route);
	}
}

/*
 *	Check that the device given is a valid X.25 interface that is "up".
 */
struct net_device *x25_dev_get(char *devname)
{
	struct net_device *dev = dev_get_by_name(devname);

	if (dev &&
	    (!(dev->flags & IFF_UP) || (dev->type != ARPHRD_X25
#if defined(CONFIG_LLC) || defined(CONFIG_LLC_MODULE)
					&& dev->type != ARPHRD_ETHER
#endif
					)))
		dev_put(dev);

	return dev;
}

/*
 *	Find a device given an X.25 address.
 */
struct net_device *x25_get_route(struct x25_address *addr)
{
	struct x25_route *route, *use = NULL;

	for (route = x25_route_list; route; route = route->next)
		if (!memcmp(&route->address, addr, route->sigdigits)) {
			if (!use)
				use = route;
			else if (route->sigdigits > use->sigdigits)
				use = route;
		}

	return use ? use->dev : NULL;
}

/*
 *	Handle the ioctls that control the routing functions.
 */
int x25_route_ioctl(unsigned int cmd, void *arg)
{
	struct x25_route_struct x25_route;
	struct net_device *dev;
	int rc = -EINVAL;

	if (cmd != SIOCADDRT && cmd != SIOCDELRT)
		goto out;

	rc = -EFAULT;
	if (copy_from_user(&x25_route, arg, sizeof(x25_route)))
		goto out;

	rc = -EINVAL;
	if (x25_route.sigdigits < 0 || x25_route.sigdigits > 15)
		goto out;

	dev = x25_dev_get(x25_route.device);
	if (!dev)
		goto out;

	if (cmd == SIOCADDRT)
		rc = x25_add_route(&x25_route.address, x25_route.sigdigits, dev);
	else
		rc = x25_del_route(&x25_route.address, x25_route.sigdigits, dev);
	dev_put(dev);
out:
	return rc;
}

int x25_routes_get_info(char *buffer, char **start, off_t offset, int length)
{
	struct x25_route *x25_route;
	int len;
	off_t pos   = 0;
	off_t begin = 0;

	cli();

	len = sprintf(buffer, "address          digits  device\n");

	for (x25_route = x25_route_list; x25_route; x25_route = x25_route->next) {
		len += sprintf(buffer + len, "%-15s  %-6d  %-5s\n",
			       x25_route->address.x25_addr,
			       x25_route->sigdigits,
			       x25_route->dev ? x25_route->dev->name : "???");

		pos = begin + len;

		if (pos < offset) {
			len   = 0;
			begin = pos;
		}

		if (pos > offset + length)
			break;
	}

	sti();

	*start = buffer + (offset - begin);
	len   -= (offset - begin);

	if (len > length)
		len = length;

	return len;
} 

/*
 *	Release all memory associated with X.25 routing structures.
 */
void __exit x25_route_free(void)
{
	struct x25_route *route, *x25_route = x25_route_list;

	while (x25_route) {
		route     = x25_route;
		x25_route = x25_route->next;

		x25_remove_route(route);
	}
}
