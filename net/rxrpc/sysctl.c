/* sysctl.c: Rx RPC control
 *
 * Copyright (C) 2002 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#include <linux/config.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/sysctl.h>
#include <linux/config.h>
#include <rxrpc/types.h>
#include <rxrpc/rxrpc.h>
#include <asm/errno.h>
#include "internal.h"

int rxrpc_ktrace;
int rxrpc_kdebug;
int rxrpc_kproto;
int rxrpc_knet;

#ifdef CONFIG_SYSCTL
static struct ctl_table_header *rxrpc_sysctl = NULL;

static ctl_table rxrpc_sysctl_table[] = {
        { 1, "kdebug", &rxrpc_kdebug, sizeof(int), 0644, NULL, &proc_dointvec },
        { 2, "ktrace", &rxrpc_ktrace, sizeof(int), 0644, NULL, &proc_dointvec },
        { 3, "kproto", &rxrpc_kproto, sizeof(int), 0644, NULL, &proc_dointvec },
        { 4, "knet",   &rxrpc_knet,   sizeof(int), 0644, NULL, &proc_dointvec },
	{ 0 }
};

static ctl_table rxrpc_dir_sysctl_table[] = {
	{ 1, "rxrpc", NULL, 0, 0555, rxrpc_sysctl_table },
	{ 0 }
};
#endif /* CONFIG_SYSCTL */

/*****************************************************************************/
/*
 * initialise the sysctl stuff for Rx RPC
 */
int rxrpc_sysctl_init(void)
{
#ifdef CONFIG_SYSCTL
	rxrpc_sysctl = register_sysctl_table(rxrpc_dir_sysctl_table,0);
	if (!rxrpc_sysctl)
		return -ENOMEM;
#endif /* CONFIG_SYSCTL */

	return 0;
} /* end rxrpc_sysctl_init() */

/*****************************************************************************/
/*
 * clean up the sysctl stuff for Rx RPC
 */
void rxrpc_sysctl_cleanup(void)
{
#ifdef CONFIG_SYSCTL
	if (rxrpc_sysctl) {
		unregister_sysctl_table(rxrpc_sysctl);
		rxrpc_sysctl = NULL;
	}
#endif /* CONFIG_SYSCTL */

} /* end rxrpc_sysctl_cleanup() */
