/*
 * proc_llc.c - proc interface for LLC
 *
 * Copyright (c) 2001 by Jay Schulist <jschlst@samba.org>
 *		 2002 by Arnaldo Carvalho de Melo <acme@conectiva.com.br>
 *
 * This program can be redistributed or modified under the terms of the
 * GNU General Public License as published by the Free Software Foundation.
 * This program is distributed without any warranty or implied warranty
 * of merchantability or fitness for a particular purpose.
 *
 * See the GNU General Public License for more details.
 */
#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/socket.h>
#include <linux/sockios.h>
#include <asm/uaccess.h>
#include <asm/ioctls.h>
#include <linux/proc_fs.h>
#include <linux/in.h>
#include <linux/tcp.h>
#include <linux/netdevice.h>
#include <linux/inetdevice.h>
#include <linux/errno.h>
#include <net/sock.h>
#include <net/llc_if.h>
#include <net/llc_sap.h>
#include <net/llc_pdu.h>
#include <net/llc_conn.h>
#include <net/llc_mac.h>
#include <net/llc_main.h>
#include <linux/llc.h>
#include <linux/if_arp.h>
#include <linux/rtnetlink.h>
#include <linux/init.h>
#include <linux/seq_file.h>

#ifdef CONFIG_PROC_FS
static void llc_ui_format_mac(struct seq_file *seq, unsigned char *mac)
{
	seq_printf(seq, "%02X:%02X:%02X:%02X:%02X:%02X",
		   mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

static __inline__ struct sock *llc_get_sk_idx(loff_t pos)
{
	struct list_head *sap_entry;
	struct llc_sap *sap;
	struct sock *sk = NULL;

	list_for_each(sap_entry, &llc_main_station.sap_list.list) {
		sap = list_entry(sap_entry, struct llc_sap, node);

		read_lock_bh(&sap->sk_list.lock);
		for (sk = sap->sk_list.list; pos && sk; sk = sk->next)
			--pos;
		if (!pos) {
			if (!sk)
				read_unlock_bh(&sap->sk_list.lock);
			break;
		}
		read_unlock_bh(&sap->sk_list.lock);
	}
	return sk;
}

static void *llc_seq_start(struct seq_file *seq, loff_t *pos)
{
	loff_t l = *pos;

	read_lock_bh(&llc_main_station.sap_list.lock);
	if (!l)
		return (void *)1;
	return llc_get_sk_idx(--l);
}

static void *llc_seq_next(struct seq_file *seq, void *v, loff_t *pos)
{
	struct sock* sk;
	struct llc_opt *llc;
	struct llc_sap *sap;

	++*pos;
	if (v == (void *)1) {
		if (list_empty(&llc_main_station.sap_list.list)) {
			sk = NULL;
			goto out;
		}
		sap = list_entry(llc_main_station.sap_list.list.next,
				 struct llc_sap, node);

		read_lock_bh(&sap->sk_list.lock);
		sk = sap->sk_list.list;
		goto out;
	}
	sk = v;
	if (sk->next) {
		sk = sk->next;
		goto out;
	}
	llc = llc_sk(sk);
	sap = llc->sap;
	read_unlock_bh(&sap->sk_list.lock);
	sk = NULL;
	for (;;) {
		if (sap->node.next == &llc_main_station.sap_list.list)
			break;
		sap = list_entry(sap->node.next, struct llc_sap, node);
		read_lock_bh(&sap->sk_list.lock);
		if (sap->sk_list.list) {
			sk = sap->sk_list.list;
			break;
		}
		read_unlock_bh(&sap->sk_list.lock);
	}
out:
	return sk;
}

static void llc_seq_stop(struct seq_file *seq, void *v)
{
	read_unlock_bh(&llc_main_station.sap_list.lock);
}

static int llc_seq_show(struct seq_file *seq, void *v)
{
	struct sock* sk;
	struct llc_opt *llc;

	if (v == (void *)1) {
		seq_puts(seq, "SKt Mc local_mac_sap        remote_mac_sap   "
			      "    tx_queue rx_queue st uid link\n");
		goto out;
	}
	sk = v;
	llc = llc_sk(sk);

	seq_printf(seq, "%2X  %2X ", sk->type,
		   !llc_mac_null(llc->addr.sllc_mmac));

	if (llc->dev && llc_mac_null(llc->addr.sllc_mmac))
		llc_ui_format_mac(seq, llc->dev->dev_addr);
	else if (!llc_mac_null(llc->addr.sllc_mmac))
		llc_ui_format_mac(seq, llc->addr.sllc_mmac);
	else
		seq_printf(seq, "00:00:00:00:00:00");
	seq_printf(seq, "@%02X ", llc->sap->laddr.lsap);
	llc_ui_format_mac(seq, llc->addr.sllc_dmac);
	seq_printf(seq, "@%02X %8d %8d %2d %3d %4d\n", llc->addr.sllc_dsap,
		   atomic_read(&sk->wmem_alloc), atomic_read(&sk->rmem_alloc),
		   sk->state, sk->socket ? SOCK_INODE(sk->socket)->i_uid : -1,
		   llc->link);
out:
	return 0;
}

struct seq_operations llc_seq_ops = {
	.start  = llc_seq_start,
	.next   = llc_seq_next,
	.stop   = llc_seq_stop,
	.show   = llc_seq_show,
};

static int llc_seq_open(struct inode *inode, struct file *file)
{
	return seq_open(file, &llc_seq_ops);
}

static int llc_proc_perms(struct inode* inode, int op)
{
	return 0;
}

static struct file_operations llc_seq_fops = {
	.open		= llc_seq_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= seq_release,
};

static struct inode_operations llc_seq_inode = {
	.permission	= llc_proc_perms,
};

static struct proc_dir_entry *llc_proc_dir;

int __init llc_proc_init(void)
{
	int rc = -ENOMEM;
	struct proc_dir_entry *p;

	llc_proc_dir = proc_mkdir("llc", proc_net);
	if (!llc_proc_dir)
		goto out;

	p = create_proc_entry("socket", 0, llc_proc_dir);
	if (!p)
		goto out_socket;

	p->proc_fops = &llc_seq_fops;
	p->proc_iops = &llc_seq_inode;
	rc = 0;
out:
	return rc;
out_socket:
	remove_proc_entry("llc", proc_net);
	goto out;
}

void __exit llc_proc_exit(void)
{
	remove_proc_entry("socket", llc_proc_dir);
	remove_proc_entry("llc", proc_net);
}
#else /* CONFIG_PROC_FS */
int __init llc_proc_init(void)
{
	return 0;
}

void __exit llc_proc_exit(void)
{
}
#endif /* CONFIG_PROC_FS */
