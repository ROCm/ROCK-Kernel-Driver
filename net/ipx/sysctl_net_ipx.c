/* -*- linux-c -*-
 * sysctl_net_ipx.c: sysctl interface to net IPX subsystem.
 *
 * Begun April 1, 1996, Mike Shaver.
 * Added /proc/sys/net/ipx directory entry (empty =) ). [MS]
 */

#include <linux/mm.h>
#include <linux/sysctl.h>

ctl_table ipx_table[] = {
	{0}
};
