/*
 *	Linux ethernet bridge
 *
 *	Authors:
 *	Lennert Buytenhek		<buytenh@gnu.org>
 *
 *	$Id: br_private.h,v 1.7 2001/12/24 00:59:55 davem Exp $
 *
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License
 *	as published by the Free Software Foundation; either version
 *	2 of the License, or (at your option) any later version.
 */

#ifndef _BR_PRIVATE_H
#define _BR_PRIVATE_H

#include <linux/netdevice.h>
#include <linux/miscdevice.h>
#include <linux/if_bridge.h>

#define BR_HASH_BITS 8
#define BR_HASH_SIZE (1 << BR_HASH_BITS)

#define BR_HOLD_TIME (1*HZ)

typedef struct bridge_id bridge_id;
typedef struct mac_addr mac_addr;
typedef __u16 port_id;

struct bridge_id
{
	unsigned char	prio[2];
	unsigned char	addr[6];
};

struct mac_addr
{
	unsigned char	addr[6];
	unsigned char	pad[2];
};

struct net_bridge_fdb_entry
{
	struct hlist_node		hlist;
	struct net_bridge_port		*dst;
	struct list_head		age_list;
	atomic_t			use_count;
	unsigned long			ageing_timer;
	mac_addr			addr;
	unsigned			is_local:1;
	unsigned			is_static:1;
};

struct net_bridge_port
{
	struct net_bridge		*br;
	struct net_device		*dev;
	struct list_head		list;
	__u8				port_no;
	__u8				priority;

	/* STP */
	port_id				port_id;
	int				state;
	int				path_cost;
	bridge_id			designated_root;
	int				designated_cost;
	bridge_id			designated_bridge;
	port_id				designated_port;
	unsigned			topology_change_ack:1;
	unsigned			config_pending:1;

	struct timer_list		forward_delay_timer;
	struct timer_list		hold_timer;
	struct timer_list		message_age_timer;
	struct rcu_head			rcu;
};

struct net_bridge
{
	spinlock_t			lock;
	struct list_head		port_list;
	struct net_device		*dev;
	struct net_device_stats		statistics;
	rwlock_t			hash_lock;
	struct hlist_head		hash[BR_HASH_SIZE];
	struct list_head		age_list;

	/* STP */
	bridge_id			designated_root;
	int				root_path_cost;
	int				root_port;
	int				max_age;
	int				hello_time;
	int				forward_delay;
	bridge_id			bridge_id;
	int				bridge_max_age;
	int				bridge_hello_time;
	int				bridge_forward_delay;
	unsigned			stp_enabled:1;
	unsigned			topology_change:1;
	unsigned			topology_change_detected:1;

	struct timer_list		hello_timer;
	struct timer_list		tcn_timer;
	struct timer_list		topology_change_timer;
	struct timer_list		gc_timer;

	int				ageing_time;
};

extern struct notifier_block br_device_notifier;
extern unsigned char bridge_ula[6];

/* called under bridge lock */
static inline int br_is_root_bridge(const struct net_bridge *br)
{
	return !memcmp(&br->bridge_id, &br->designated_root, 8);
}


/* br_device.c */
extern void br_dev_setup(struct net_device *dev);
extern int br_dev_xmit(struct sk_buff *skb, struct net_device *dev);

/* br_fdb.c */
extern void br_fdb_changeaddr(struct net_bridge_port *p,
			      const unsigned char *newaddr);
extern void br_fdb_cleanup(unsigned long arg);
extern void br_fdb_delete_by_port(struct net_bridge *br,
			   struct net_bridge_port *p);
extern struct net_bridge_fdb_entry *br_fdb_get(struct net_bridge *br,
					unsigned char *addr);
extern void br_fdb_put(struct net_bridge_fdb_entry *ent);
extern int  br_fdb_get_entries(struct net_bridge *br,
			unsigned char *_buf,
			int maxnum,
			int offset);
extern void br_fdb_insert(struct net_bridge *br,
			  struct net_bridge_port *source,
			  const unsigned char *addr,
			  int is_local);

/* br_forward.c */
extern void br_deliver(const struct net_bridge_port *to,
		struct sk_buff *skb);
extern int br_dev_queue_push_xmit(struct sk_buff *skb);
extern void br_forward(const struct net_bridge_port *to,
		struct sk_buff *skb);
extern int br_forward_finish(struct sk_buff *skb);
extern void br_flood_deliver(struct net_bridge *br,
		      struct sk_buff *skb,
		      int clone);
extern void br_flood_forward(struct net_bridge *br,
		      struct sk_buff *skb,
		      int clone);

/* br_if.c */
extern int br_add_bridge(const char *name);
extern int br_del_bridge(const char *name);
extern void br_cleanup_bridges(void);
extern int br_add_if(struct net_bridge *br,
	      struct net_device *dev);
extern int br_del_if(struct net_bridge *br,
	      struct net_device *dev);
extern int br_get_bridge_ifindices(int *indices,
			    int num);
extern void br_get_port_ifindices(struct net_bridge *br,
			   int *ifindices, int num);

/* br_input.c */
extern int br_handle_frame_finish(struct sk_buff *skb);
extern int br_handle_frame(struct sk_buff *skb);

/* br_ioctl.c */
extern int br_ioctl_device(struct net_bridge *br,
			   unsigned int cmd,
			   unsigned long arg0,
			   unsigned long arg1,
			   unsigned long arg2);
extern int br_ioctl_deviceless_stub(unsigned long arg);

/* br_netfilter.c */
extern int br_netfilter_init(void);
extern void br_netfilter_fini(void);

/* br_stp.c */
extern void br_log_state(const struct net_bridge_port *p);
extern struct net_bridge_port *br_get_port(struct net_bridge *br,
				    int port_no);
extern void br_init_port(struct net_bridge_port *p);
extern void br_become_designated_port(struct net_bridge_port *p);

/* br_stp_if.c */
extern void br_stp_enable_bridge(struct net_bridge *br);
extern void br_stp_disable_bridge(struct net_bridge *br);
extern void br_stp_enable_port(struct net_bridge_port *p);
extern void br_stp_disable_port(struct net_bridge_port *p);
extern void br_stp_recalculate_bridge_id(struct net_bridge *br);
extern void br_stp_set_bridge_priority(struct net_bridge *br,
				int newprio);
extern void br_stp_set_port_priority(struct net_bridge_port *p,
			      int newprio);
extern void br_stp_set_path_cost(struct net_bridge_port *p,
			  int path_cost);

/* br_stp_bpdu.c */
extern int br_stp_handle_bpdu(struct sk_buff *skb);

/* br_stp_timer.c */
extern void br_stp_timer_init(struct net_bridge *br);
extern void br_stp_port_timer_init(struct net_bridge_port *p);

#endif
