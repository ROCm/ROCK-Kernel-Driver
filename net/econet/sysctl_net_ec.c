/*
 *	An implementation of the Acorn Econet and AUN protocols.
 *	Philip Blundell <philb@gnu.org>
 *
 *	Fixes:
 *
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License
 *	as published by the Free Software Foundation; either version
 *	2 of the License, or (at your option) any later version.
 *
 */

#include <linux/mm.h>
#include <linux/sysctl.h>

ctl_table econet_table[] = {
	{0}
};

static struct ctl_table_header *econet_sysctl_header;

static ctl_table econet_net_table[] = {
	{NET_ECONET, "econet", NULL, 0, 0555, econet_table},
        {0}
};

static ctl_table econet_root_table[] = {
	{CTL_NET, "net", NULL, 0, 0555, econet_net_table},
        {0}
};

void econet_sysctl_register(void)
{
	econet_sysctl_header = register_sysctl_table(econet_root_table, 0);
}

#ifdef MODULE
void econet_sysctl_unregister(void)
{
	unregister_sysctl_table(econet_sysctl_header);
}
#endif	/* MODULE */
