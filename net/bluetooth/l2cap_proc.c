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
 * BlueZ L2CAP proc fs support.
 *
 * $Id: l2cap_proc.c,v 1.2 2001/06/02 01:40:09 maxk Exp $
 */

#include <linux/config.h>
#include <linux/module.h>

#include <linux/types.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/major.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/poll.h>
#include <linux/fcntl.h>
#include <linux/init.h>
#include <linux/skbuff.h>
#include <linux/interrupt.h>
#include <linux/socket.h>
#include <linux/skbuff.h>
#include <linux/proc_fs.h>
#include <linux/list.h>
#include <net/sock.h>

#include <asm/system.h>
#include <asm/uaccess.h>

#include <net/bluetooth/bluez.h>
#include <net/bluetooth/bluetooth.h>
#include <net/bluetooth/hci_core.h>
#include <net/bluetooth/l2cap_core.h>

#ifndef L2CAP_DEBUG
#undef  DBG
#define DBG( A... )
#endif

/* ----- PROC fs support ----- */
static int l2cap_conn_dump(char *buf, struct l2cap_iff *iff)
{
	struct list_head *p;
	char *ptr = buf;

	list_for_each(p, &iff->conn_list) {
		struct l2cap_conn *c;

		c = list_entry(p, struct l2cap_conn, list);
		ptr += sprintf(ptr, "    %p %d %p %p %s %s\n", 
				c, c->state, c->iff, c->hconn, batostr(&c->src), batostr(&c->dst));
	}

	return ptr - buf;
}

static int l2cap_iff_dump(char *buf)
{
	struct list_head *p;
	char *ptr = buf;

	ptr += sprintf(ptr, "Interfaces:\n");

	write_lock(&l2cap_rt_lock);

	list_for_each(p, &l2cap_iff_list) {
		struct l2cap_iff *iff;

		iff = list_entry(p, struct l2cap_iff, list);

		ptr += sprintf(ptr, "  %s %p %p\n", iff->hdev->name, iff, iff->hdev);
		
		l2cap_iff_lock(iff);
		ptr += l2cap_conn_dump(ptr, iff);
		l2cap_iff_unlock(iff);
	}

	write_unlock(&l2cap_rt_lock);

	ptr += sprintf(ptr, "\n");

	return ptr - buf;
}

static int l2cap_sock_dump(char *buf, struct bluez_sock_list *list)
{
	struct l2cap_pinfo *pi;
	struct sock *sk;
	char *ptr = buf;

	ptr += sprintf(ptr, "Sockets:\n");

	write_lock(&list->lock);

	for (sk = list->head; sk; sk = sk->next) {
		pi = l2cap_pi(sk);
		ptr += sprintf(ptr, "  %p %d %p %d %s %s 0x%4.4x 0x%4.4x %d %d\n", sk, sk->state, pi->conn, pi->psm,
		               batostr(&pi->src), batostr(&pi->dst), pi->scid, pi->dcid, pi->imtu, pi->omtu );
	}

	write_unlock(&list->lock);

	ptr += sprintf(ptr, "\n");

	return ptr - buf;
}

static int l2cap_read_proc(char *buf, char **start, off_t offset, int count, int *eof, void *priv)
{
	char *ptr = buf;
	int len;

	DBG("count %d, offset %ld", count, offset);

	ptr += l2cap_iff_dump(ptr);
	ptr += l2cap_sock_dump(ptr, &l2cap_sk_list);
	len  = ptr - buf;

	if (len <= count + offset)
		*eof = 1;

	*start = buf + offset;
	len -= offset;

	if (len > count)
		len = count;
	if (len < 0)
		len = 0;

	return len;
}

void l2cap_register_proc(void)
{
	create_proc_read_entry("bluetooth/l2cap", 0, 0, l2cap_read_proc, NULL);
}

void l2cap_unregister_proc(void)
{
	remove_proc_entry("bluetooth/l2cap", NULL);
}
