/*
 *	Forwarding database
 *	Linux ethernet bridge
 *
 *	Authors:
 *	Lennert Buytenhek		<buytenh@gnu.org>
 *
 *	$Id: br_fdb.c,v 1.6 2002/01/17 00:57:07 davem Exp $
 *
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License
 *	as published by the Free Software Foundation; either version
 *	2 of the License, or (at your option) any later version.
 */

#include <linux/kernel.h>
#include <linux/spinlock.h>
#include <linux/if_bridge.h>
#include <asm/atomic.h>
#include <asm/uaccess.h>
#include "br_private.h"

static __inline__ unsigned long __timeout(struct net_bridge *br)
{
	unsigned long timeout;

	timeout = jiffies - br->ageing_time;
	if (br->topology_change)
		timeout = jiffies - br->forward_delay;

	return timeout;
}

static __inline__ int has_expired(struct net_bridge *br,
				  struct net_bridge_fdb_entry *fdb)
{
	if (!fdb->is_static &&
	    time_before_eq(fdb->ageing_timer, __timeout(br)))
		return 1;

	return 0;
}

static __inline__ void copy_fdb(struct __fdb_entry *ent, 
				const struct net_bridge_fdb_entry *f)
{
	memset(ent, 0, sizeof(struct __fdb_entry));
	memcpy(ent->mac_addr, f->addr.addr, ETH_ALEN);
	ent->port_no = f->dst?f->dst->port_no:0;
	ent->is_local = f->is_local;
	ent->ageing_timer_value = f->is_static ? 0 
		: ((jiffies - f->ageing_timer) * USER_HZ) / HZ;
}

static __inline__ int br_mac_hash(unsigned char *mac)
{
	unsigned long x;

	x = mac[0];
	x = (x << 2) ^ mac[1];
	x = (x << 2) ^ mac[2];
	x = (x << 2) ^ mac[3];
	x = (x << 2) ^ mac[4];
	x = (x << 2) ^ mac[5];

	x ^= x >> 8;

	return x & (BR_HASH_SIZE - 1);
}

void br_fdb_changeaddr(struct net_bridge_port *p, unsigned char *newaddr)
{
	struct net_bridge *br;
	int i;
	int newhash = br_mac_hash(newaddr);

	br = p->br;
	write_lock_bh(&br->hash_lock);
	for (i=0;i<BR_HASH_SIZE;i++) {
		struct hlist_node *h;
		
		hlist_for_each(h, &br->hash[i]) {
			struct net_bridge_fdb_entry *f
				= hlist_entry(h, struct net_bridge_fdb_entry, hlist);

			if (f->dst == p && f->is_local) {
				memcpy(f->addr.addr, newaddr, ETH_ALEN);
				if (newhash != i) {
					hlist_del(&f->hlist);
					hlist_add_head(&f->hlist,
						       &br->hash[newhash]);
				}
				goto out;
			}
		}
	}
 out:
	write_unlock_bh(&br->hash_lock);
}

void br_fdb_cleanup(struct net_bridge *br)
{
	int i;
	unsigned long timeout;

	timeout = __timeout(br);

	write_lock_bh(&br->hash_lock);
	for (i=0;i<BR_HASH_SIZE;i++) {
		struct hlist_node *h, *g;
		
		hlist_for_each_safe(h, g, &br->hash[i]) {
			struct net_bridge_fdb_entry *f
				= hlist_entry(h, struct net_bridge_fdb_entry, hlist);
			if (!f->is_static &&
			    time_before_eq(f->ageing_timer, timeout)) {
				hlist_del(&f->hlist);
				br_fdb_put(f);
			}
		}
	}
	write_unlock_bh(&br->hash_lock);
}

void br_fdb_delete_by_port(struct net_bridge *br, struct net_bridge_port *p)
{
	int i;

	write_lock_bh(&br->hash_lock);
	for (i=0;i<BR_HASH_SIZE;i++) {
		struct hlist_node *h, *g;
		
		hlist_for_each_safe(h, g, &br->hash[i]) {
			struct net_bridge_fdb_entry *f
				= hlist_entry(h, struct net_bridge_fdb_entry, hlist);
			if (f->dst == p) {
				hlist_del(&f->hlist);
				br_fdb_put(f);
			}
		}
	}
	write_unlock_bh(&br->hash_lock);
}

struct net_bridge_fdb_entry *br_fdb_get(struct net_bridge *br, unsigned char *addr)
{
	struct hlist_node *h;

	read_lock_bh(&br->hash_lock);
		
	hlist_for_each(h, &br->hash[br_mac_hash(addr)]) {
		struct net_bridge_fdb_entry *fdb
			= hlist_entry(h, struct net_bridge_fdb_entry, hlist);

		if (!memcmp(fdb->addr.addr, addr, ETH_ALEN)) {
			if (has_expired(br, fdb))
				goto ret_null;

			atomic_inc(&fdb->use_count);
			read_unlock_bh(&br->hash_lock);
			return fdb;
		}
	}
 ret_null:
	read_unlock_bh(&br->hash_lock);
	return NULL;
}

void br_fdb_put(struct net_bridge_fdb_entry *ent)
{
	if (atomic_dec_and_test(&ent->use_count))
		kfree(ent);
}

int br_fdb_get_entries(struct net_bridge *br,
		       unsigned char *_buf,
		       int maxnum,
		       int offset)
{
	int i;
	int num;
	struct __fdb_entry *walk;

	num = 0;
	walk = (struct __fdb_entry *)_buf;

	read_lock_bh(&br->hash_lock);
	for (i=0;i<BR_HASH_SIZE;i++) {
		struct hlist_node *h;
		
		hlist_for_each(h, &br->hash[i]) {
			struct net_bridge_fdb_entry *f
				= hlist_entry(h, struct net_bridge_fdb_entry, hlist);
			struct __fdb_entry ent;

			if (num >= maxnum)
				goto out;

			if (has_expired(br, f)) 
				continue;

			if (offset) {
				offset--;
				continue;
			}

			copy_fdb(&ent, f);

			atomic_inc(&f->use_count);
			read_unlock_bh(&br->hash_lock);
			
			if (copy_to_user(walk, &ent, sizeof(struct __fdb_entry)))
				return -EFAULT;

			read_lock_bh(&br->hash_lock);
			
			/* entry was deleted during copy_to_user */
			if (atomic_dec_and_test(&f->use_count)) {
				kfree(f);
				num = -EAGAIN;
				goto out;
			}

			/* entry changed address hash while copying */
			if (br_mac_hash(f->addr.addr) != i) {
				num = -EAGAIN;
				goto out;
			}

			num++;
			walk++;
		}
	}

 out:
	read_unlock_bh(&br->hash_lock);
	return num;
}

static __inline__ void __fdb_possibly_replace(struct net_bridge_fdb_entry *fdb,
					      struct net_bridge_port *source,
					      int is_local)
{
	if (!fdb->is_static || is_local) {
		fdb->dst = source;
		fdb->is_local = is_local;
		fdb->is_static = is_local;
		fdb->ageing_timer = jiffies;
	}
}

void br_fdb_insert(struct net_bridge *br,
		   struct net_bridge_port *source,
		   unsigned char *addr,
		   int is_local)
{
	struct hlist_node *h;
	struct net_bridge_fdb_entry *fdb;
	int hash;

	hash = br_mac_hash(addr);

	write_lock_bh(&br->hash_lock);
	hlist_for_each(h, &br->hash[hash]) {
		fdb = hlist_entry(h, struct net_bridge_fdb_entry, hlist);
		if (!fdb->is_local &&
		    !memcmp(fdb->addr.addr, addr, ETH_ALEN)) {
			__fdb_possibly_replace(fdb, source, is_local);
			write_unlock_bh(&br->hash_lock);
			return;
		}

	}

	fdb = kmalloc(sizeof(*fdb), GFP_ATOMIC);
	if (fdb == NULL) {
		write_unlock_bh(&br->hash_lock);
		return;
	}

	memcpy(fdb->addr.addr, addr, ETH_ALEN);
	atomic_set(&fdb->use_count, 1);
	fdb->dst = source;
	fdb->is_local = is_local;
	fdb->is_static = is_local;
	fdb->ageing_timer = jiffies;

	hlist_add_head(&fdb->hlist, &br->hash[hash]);

	write_unlock_bh(&br->hash_lock);
}
