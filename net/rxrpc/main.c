/* main.c: Rx RPC interface
 *
 * Copyright (C) 2002 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/sched.h>
#include <rxrpc/rxrpc.h>
#include <rxrpc/krxiod.h>
#include <rxrpc/krxsecd.h>
#include <rxrpc/krxtimod.h>
#include <rxrpc/transport.h>
#include <rxrpc/connection.h>
#include <rxrpc/call.h>
#include <rxrpc/message.h>
#include "internal.h"

static int rxrpc_initialise(void);
static void rxrpc_cleanup(void);

module_init(rxrpc_initialise);
module_exit(rxrpc_cleanup);

MODULE_DESCRIPTION("Rx RPC implementation");
MODULE_AUTHOR("Red Hat, Inc.");
MODULE_LICENSE("GPL");

u32 rxrpc_epoch;

/*****************************************************************************/
/*
 * initialise the Rx module
 */
static int rxrpc_initialise(void)
{
	int ret;

	/* my epoch value */
	rxrpc_epoch = htonl(xtime.tv_sec);

	/* register the /proc interface */
#ifdef CONFIG_PROC_FS
	ret = rxrpc_proc_init();
	if (ret<0)
		return ret;
#endif

	/* register the sysctl files */
#ifdef CONFIG_SYSCTL
	ret = rxrpc_sysctl_init();
	if (ret<0)
		goto error_proc;
#endif

	/* start the krxtimod daemon */
	ret = rxrpc_krxtimod_start();
	if (ret<0)
		goto error_sysctl;

	/* start the krxiod daemon */
	ret = rxrpc_krxiod_init();
	if (ret<0)
		goto error_krxtimod;

	/* start the krxsecd daemon */
	ret = rxrpc_krxsecd_init();
	if (ret<0)
		goto error_krxiod;

	kdebug("\n\n");

	return 0;

 error_krxiod:
	rxrpc_krxiod_kill();
 error_krxtimod:
	rxrpc_krxtimod_kill();
 error_sysctl:
#ifdef CONFIG_SYSCTL
	rxrpc_sysctl_cleanup();
#endif
 error_proc:
#ifdef CONFIG_PROC_FS
	rxrpc_proc_cleanup();
#endif
	return ret;
} /* end rxrpc_initialise() */

/*****************************************************************************/
/*
 * clean up the Rx module
 */
static void rxrpc_cleanup(void)
{
	kenter("");

	__RXACCT(printk("Outstanding Messages   : %d\n",atomic_read(&rxrpc_message_count)));
	__RXACCT(printk("Outstanding Calls      : %d\n",atomic_read(&rxrpc_call_count)));
	__RXACCT(printk("Outstanding Connections: %d\n",atomic_read(&rxrpc_connection_count)));
	__RXACCT(printk("Outstanding Peers      : %d\n",atomic_read(&rxrpc_peer_count)));
	__RXACCT(printk("Outstanding Transports : %d\n",atomic_read(&rxrpc_transport_count)));

	rxrpc_krxsecd_kill();
	rxrpc_krxiod_kill();
	rxrpc_krxtimod_kill();
#ifdef CONFIG_SYSCTL
	rxrpc_sysctl_cleanup();
#endif
#ifdef CONFIG_PROC_FS
	rxrpc_proc_cleanup();
#endif

	__RXACCT(printk("Outstanding Messages   : %d\n",atomic_read(&rxrpc_message_count)));
	__RXACCT(printk("Outstanding Calls      : %d\n",atomic_read(&rxrpc_call_count)));
	__RXACCT(printk("Outstanding Connections: %d\n",atomic_read(&rxrpc_connection_count)));
	__RXACCT(printk("Outstanding Peers      : %d\n",atomic_read(&rxrpc_peer_count)));
	__RXACCT(printk("Outstanding Transports : %d\n",atomic_read(&rxrpc_transport_count)));

	kleave();
} /* end rxrpc_cleanup() */
