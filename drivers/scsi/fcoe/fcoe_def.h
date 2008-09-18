/*
 * Copyright(c) 2007 Intel Corporation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin St - Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * Maintained at www.Open-FCoE.org
 */

#ifndef _FCOE_DEF_H_
#define _FCOE_DEF_H_

#include <linux/etherdevice.h>
#include <linux/if_ether.h>

#include <scsi/libfc/libfc.h>

#include <scsi/fc/fc_fcoe.h>

#define	FCOE_DRIVER_NAME    "fcoe"	/* driver name for ioctls */
#define	FCOE_DRIVER_VENDOR  "Open-FC.org" /* vendor name for ioctls */

#define FCOE_MIN_FRAME	36
#define FCOE_WORD_TO_BYTE  4

/*
 * this is the main  common structure across all instance of fcoe driver.
 * There is one to one mapping between hba struct and ethernet nic.
 * list of hbas contains pointer to the hba struct, these structures are
 * stored in this array using there corresponding if_index.
 */

struct fcoe_percpu_s {
	int		cpu;
	struct task_struct *thread;
	struct sk_buff_head fcoe_rx_list;
	struct page *crc_eof_page;
	int crc_eof_offset;
};

struct fcoe_info {
	struct timer_list timer;
	/*
	 * fcoe host list is protected by the following read/write lock
	 */
	rwlock_t fcoe_hostlist_lock;
	struct list_head fcoe_hostlist;

	struct fcoe_percpu_s *fcoe_percpu[NR_CPUS];
};

struct fcoe_softc {
	struct list_head list;
	struct fc_lport *lp;
	struct net_device *real_dev;
	struct net_device *phys_dev;		/* device with ethtool_ops */
	struct packet_type  fcoe_packet_type;
	struct sk_buff_head fcoe_pending_queue;
	u16 user_mfs;			/* configured max frame size */

	u8 dest_addr[ETH_ALEN];
	u8 ctl_src_addr[ETH_ALEN];
	u8 data_src_addr[ETH_ALEN];
	/*
	 * fcoe protocol address learning related stuff
	 */
	u16 flogi_oxid;
	u8 flogi_progress;
	u8 address_mode;
};

extern int debug_fcoe;
extern struct fcoe_percpu_s *fcoe_percpu[];
extern struct scsi_transport_template *fcoe_transport_template;
int fcoe_percpu_receive_thread(void *arg);

/*
 * HBA transport ops prototypes
 */
extern struct fcoe_info fcoei;

void fcoe_clean_pending_queue(struct fc_lport *fd);
void fcoe_watchdog(ulong vp);
int fcoe_destroy_interface(const char *ifname);
int fcoe_create_interface(const char *ifname);
int fcoe_xmit(struct fc_lport *, struct fc_frame *);
int fcoe_rcv(struct sk_buff *, struct net_device *,
	     struct packet_type *, struct net_device *);
int fcoe_link_ok(struct fc_lport *);
#endif /* _FCOE_DEF_H_ */
