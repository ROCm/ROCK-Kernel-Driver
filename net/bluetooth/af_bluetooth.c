/* 
   BlueZ - Bluetooth protocol stack for Linux
   Copyright (C) 2000-2001 Qualcomm Incorporated

   Written 2000,2001 by Maxim Krasnyansky <maxk@qualcomm.com>

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License version 2 as
   published by the Free Software Foundation;

   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
   OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
   FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT OF THIRD PARTY RIGHTS.
   IN NO EVENT SHALL THE COPYRIGHT HOLDER(S) AND AUTHOR(S) BE LIABLE FOR ANY
   CLAIM, OR ANY SPECIAL INDIRECT OR CONSEQUENTIAL DAMAGES, OR ANY DAMAGES 
   WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN 
   ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF 
   OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.

   ALL LIABILITY, INCLUDING LIABILITY FOR INFRINGEMENT OF ANY PATENTS, 
   COPYRIGHTS, TRADEMARKS OR OTHER RIGHTS, RELATING TO USE OF THIS 
   SOFTWARE IS DISCLAIMED.
*/

/*
 * BlueZ Bluetooth address family and sockets.
 *
 * $Id: af_bluetooth.c,v 1.4 2001/07/05 18:42:44 maxk Exp $
 */
#define VERSION "1.1"

#include <linux/config.h>
#include <linux/module.h>

#include <linux/types.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/major.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/skbuff.h>
#include <linux/init.h>
#include <linux/proc_fs.h>
#include <net/sock.h>

#if defined(CONFIG_KMOD)
#include <linux/kmod.h>
#endif

#include <net/bluetooth/bluetooth.h>
#include <net/bluetooth/bluez.h>

/* Bluetooth sockets */
static struct net_proto_family *bluez_sock[BLUEZ_MAX_PROTO];

int bluez_sock_register(int proto, struct net_proto_family *ops)
{
	if (proto > BLUEZ_MAX_PROTO)
		return -EINVAL;

	if (bluez_sock[proto])
		return -EEXIST;

	bluez_sock[proto] = ops;
	return 0;
}

int bluez_sock_unregister(int proto)
{
	if (proto > BLUEZ_MAX_PROTO)
		return -EINVAL;

	if (!bluez_sock[proto])
		return -ENOENT;

	bluez_sock[proto] = NULL;
	return 0;
}

static int bluez_sock_create(struct socket *sock, int proto)
{
	if (proto > BLUEZ_MAX_PROTO)
		return -EINVAL;

#if defined(CONFIG_KMOD)
	if (!bluez_sock[proto]) {
		char module_name[30];
		sprintf(module_name, "bt-proto-%d", proto);
		request_module(module_name);
	}
#endif

	if (!bluez_sock[proto])
		return -ENOENT;

	return bluez_sock[proto]->create(sock, proto);
}

void bluez_sock_link(struct bluez_sock_list *l, struct sock *sk)
{
	write_lock(&l->lock);

	sk->next = l->head;
	l->head = sk;
	sock_hold(sk);

	write_unlock(&l->lock);
}

void bluez_sock_unlink(struct bluez_sock_list *l, struct sock *sk)
{
	struct sock **skp;

	write_lock(&l->lock);
	for (skp = &l->head; *skp; skp = &((*skp)->next)) {
		if (*skp == sk) {
			*skp = sk->next;
			__sock_put(sk);
			break;
		}
	}
	write_unlock(&l->lock);
}

struct net_proto_family bluez_sock_family_ops =
{
	PF_BLUETOOTH, bluez_sock_create
};

int bluez_init(void)
{
	INF("BlueZ HCI Core ver %s Copyright (C) 2000,2001 Qualcomm Inc",
		 VERSION);
	INF("Written 2000,2001 by Maxim Krasnyansky <maxk@qualcomm.com>");

	proc_mkdir("bluetooth", NULL);

	sock_register(&bluez_sock_family_ops);

	/* Init HCI Core */
	hci_core_init();

	/* Init sockets */
	hci_sock_init();

	return 0;
}

void bluez_cleanup(void)
{
	/* Release socket */
	hci_sock_cleanup();

	/* Release core */
	hci_core_cleanup();

	sock_unregister(PF_BLUETOOTH);

	remove_proc_entry("bluetooth", NULL);
}

#ifdef MODULE
module_init(bluez_init);
module_exit(bluez_cleanup);

MODULE_AUTHOR("Maxim Krasnyansky <maxk@qualcomm.com>");
MODULE_DESCRIPTION("BlueZ HCI Core ver " VERSION);
#endif
