/****************************************************************************
 * Solarflare driver for Xen network acceleration
 *
 * Copyright 2006-2008: Solarflare Communications Inc,
 *                      9501 Jeronimo Road, Suite 250,
 *                      Irvine, CA 92618, USA
 *
 * Maintained by Solarflare Communications <linux-xen-drivers@solarflare.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation, incorporated herein by reference.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 ****************************************************************************
 */

#ifndef NETBACK_ACCEL_H
#define NETBACK_ACCEL_H

#include <linux/version.h>
#include <linux/slab.h>
#include <linux/ip.h>
#include <linux/tcp.h>
#include <linux/udp.h>
#include <linux/in.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/mutex.h>
#include <linux/wait.h>

#include <xen/xenbus.h>

#include "accel_shared_fifo.h"
#include "accel_msg_iface.h"
#include "accel_util.h"

/**************************************************************************
 * Datatypes
 **************************************************************************/

#define NETBACK_ACCEL_DEFAULT_MAX_FILTERS (8)
#define NETBACK_ACCEL_DEFAULT_MAX_MCASTS (8)
#define NETBACK_ACCEL_DEFAULT_MAX_BUF_PAGES (384)
/* Variable to store module parameter for max_buf_pages */
extern unsigned sfc_netback_max_pages;

#define NETBACK_ACCEL_STATS 1

#if NETBACK_ACCEL_STATS
#define NETBACK_ACCEL_STATS_OP(x) x
#else
#define NETBACK_ACCEL_STATS_OP(x)
#endif

/*! Statistics for a given backend */
struct netback_accel_stats {
	/*! Number of eventq wakeup events */
	u64 evq_wakeups;
	/*! Number of eventq timeout events */
	u64 evq_timeouts;
	/*! Number of filters used */
	u32 num_filters;
	/*! Number of buffer pages registered */
	u32 num_buffer_pages;
};


/* Debug fs nodes for each of the above stats */
struct netback_accel_dbfs {
	struct dentry *evq_wakeups;
	struct dentry *evq_timeouts;
	struct dentry *num_filters;
	struct dentry *num_buffer_pages;
};


/*! Resource limits for a given NIC */
struct netback_accel_limits {
	int max_filters;	    /*!< Max. number of filters to use. */
	int max_mcasts;	     /*!< Max. number  of mcast subscriptions */
	int max_buf_pages;	  /*!< Max. number of pages of NIC buffers */
};


/*! The state for an instance of the back end driver. */
struct netback_accel {
	/*! mutex to protect this state */
	struct mutex bend_mutex;

	/*! Watches on xenstore */
	struct xenbus_watch domu_accel_watch;
	struct xenbus_watch config_accel_watch;

	/*! Pointer to whatever device cookie ties us in to the hypervisor */
	void *hdev_data;

	/*! FIFO indices. Next page is msg FIFOs */
	struct net_accel_shared_page *shared_page;

	/*! Defer control message processing */
	struct work_struct handle_msg;

	/*! Identifies other end VM and interface.*/
	int far_end;
	int vif_num;

	/*!< To unmap the shared pages */
	void *sh_pages_unmap;

	/* Resource tracking */
	/*! Limits on H/W & Dom0 resources */
	struct netback_accel_limits quotas;

	/* Hardware resources */
	/*! The H/W type of associated NIC */
	enum net_accel_hw_type hw_type;
	/*! State of allocation */	       
	int hw_state;
	/*! How to set up the acceleration for this hardware */
	int (*accel_setup)(struct netback_accel *); 
	/*! And how to stop it. */
	void (*accel_shutdown)(struct netback_accel *);

	/*! The physical/real net_dev for this interface */
	struct net_device *net_dev;

	/*! Magic pointer to locate state in fowarding table */
	void *fwd_priv;

	/*! Message FIFO */
	sh_msg_fifo2 to_domU;
	/*! Message FIFO */
	sh_msg_fifo2 from_domU;

	/*! General notification channel id */
	int msg_channel;
	/*! General notification channel irq */
	int msg_channel_irq;

	/*! Event channel id dedicated to network packet interrupts. */
	int net_channel; 
	/*! Event channel irq dedicated to network packets interrupts */
	int net_channel_irq; 

	/*! The MAC address the frontend goes by. */
	u8 mac[ETH_ALEN];
	/*! Driver name of associated NIC */
	char *nicname;    

	/*! Array of pointers to buffer pages mapped */
	grant_handle_t *buffer_maps; 
	u64 *buffer_addrs;
	/*! Index into buffer_maps */
	int buffer_maps_index; 
	/*! Max number of pages that domU is allowed/will request to map */
	int max_pages; 

	/*! Pointer to hardware specific private area */
	void *accel_hw_priv; 

	/*! Wait queue for changes in accelstate. */
	wait_queue_head_t state_wait_queue;

	/*! Current state of the frontend according to the xenbus
	 *  watch. */
	XenbusState frontend_state;

	/*! Current state of this backend. */
	XenbusState backend_state;

	/*! Non-zero if the backend is being removed. */
	int removing;

	/*! Non-zero if the setup_vnic has been called. */
	int vnic_is_setup;

#if NETBACK_ACCEL_STATS
	struct netback_accel_stats stats;
#endif	
#if defined(CONFIG_DEBUG_FS)
	char *dbfs_dir_name;
	struct dentry *dbfs_dir;
	struct netback_accel_dbfs dbfs;
#endif

	/*! List */
	struct netback_accel *next_bend;
};


/*
 * Values for netback_accel.hw_state.  States of resource allocation
 * we can go through
 */
/*! No hardware has yet been allocated. */
#define NETBACK_ACCEL_RES_NONE  (0)
/*! Hardware has been allocated. */
#define NETBACK_ACCEL_RES_ALLOC (1)
#define NETBACK_ACCEL_RES_FILTER (2)
#define NETBACK_ACCEL_RES_HWINFO (3)

/*! Filtering specification. This assumes that for VNIC support we
 *  will always want wildcard entries, so only specifies the
 *  destination IP/port
 */
struct netback_accel_filter_spec {
	/*! Internal, used to access efx_vi API */
	void *filter_handle; 

	/*! Destination IP in network order */
	u32 destip_be;
	/*! Destination port in network order */
	u16 destport_be;
	/*! Mac address */
	u8  mac[ETH_ALEN];
	/*! TCP or UDP */
	u8  proto;	
};


/**************************************************************************
 * From accel.c
 **************************************************************************/

/*! \brief Start up all the acceleration plugins 
 *
 * \return 0 on success, an errno on failure
 */
extern int netback_accel_init_accel(void);

/*! \brief Shut down all the acceleration plugins 
 */
extern void netback_accel_shutdown_accel(void);


/**************************************************************************
 * From accel_fwd.c
 **************************************************************************/

/*! \brief Init the forwarding infrastructure
 * \return 0 on success, or -ENOMEM if it couldn't get memory for the
 * forward table 
 */
extern int netback_accel_init_fwd(void);

/*! \brief Shut down the forwarding and free memory. */
extern void netback_accel_shutdown_fwd(void);

/*! Initialise each nic port's fowarding table */
extern void *netback_accel_init_fwd_port(void);
extern void netback_accel_shutdown_fwd_port(void *fwd_priv);

/*! \brief Add an entry to the forwarding table. 
 * \param mac : MAC address, used as hash key
 * \param ctxt : value to associate with key (can be NULL, see
 * netback_accel_fwd_set_context)
 * \return 0 on success, -ENOMEM if table was full and could no grow it
 */
extern int netback_accel_fwd_add(const __u8 *mac, void *context,
				 void *fwd_priv);

/*! \brief Remove an entry from the forwarding table. 
 * \param mac : the MAC address to remove
 * \return nothing: it is not an error if the mac was not in the table
 */
extern void netback_accel_fwd_remove(const __u8 *mac, void *fwd_priv);

/*! \brief Set the context pointer for an existing fwd table entry.
 * \param mac : key that is already present in the table
 * \param context : new value to associate with key
 * \return 0 on success, -ENOENT if mac not present in table.
 */
extern int netback_accel_fwd_set_context(const __u8 *mac, void *context,
					 void *fwd_priv);

/**************************************************************************
 * From accel_msg.c
 **************************************************************************/


/*! \brief Send the start-of-day message that handshakes with the VNIC
 *  and tells it its MAC address.
 *
 * \param bend The back end driver data structure
 * \param version The version of communication to use, e.g. NET_ACCEL_MSG_VERSION
 */
extern void netback_accel_msg_tx_hello(struct netback_accel *bend,
				       unsigned version);

/*! \brief Send a "there's a new local mac address" message 
 *
 * \param bend The back end driver data structure for the vnic to send
 * the message to 
 * \param mac Pointer to the new mac address
 */
extern void netback_accel_msg_tx_new_localmac(struct netback_accel *bend,
					      const void *mac);

/*! \brief Send a "a mac address that was local has gone away" message 
 *
 * \param bend The back end driver data structure for the vnic to send
 * the message to 
 * \param mac Pointer to the old mac address
 */
extern void netback_accel_msg_tx_old_localmac(struct netback_accel *bend,
					      const void *mac);

extern void netback_accel_set_interface_state(struct netback_accel *bend,
					      int up);

/*! \brief Process the message queue for a bend that has just
 * interrupted.
 * 
 * Demultiplexs an interrupt from the front end driver, taking
 * messages from the fifo and taking appropriate action.
 * 
 * \param bend The back end driver data structure
 */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,20)
extern void netback_accel_msg_rx_handler(struct work_struct *arg);
#else
extern void netback_accel_msg_rx_handler(void *bend_void);
#endif

/**************************************************************************
 * From accel_xenbus.c
 **************************************************************************/
/*! List of all the bends currently in existence. */
extern struct netback_accel *bend_list;
extern struct mutex bend_list_mutex;

/*! \brief Probe a new network interface. */
extern int netback_accel_probe(struct xenbus_device *dev);

/*! \brief Remove a network interface. */
extern int netback_accel_remove(struct xenbus_device *dev);

/*! \brief Shutdown all accelerator backends */
extern void netback_accel_shutdown_bends(void);

/*! \brief Initiate the xenbus state teardown handshake */
extern void netback_accel_set_closing(struct netback_accel *bend);

/**************************************************************************
 * From accel_debugfs.c
 **************************************************************************/
/*! Global statistics */
struct netback_accel_global_stats {
	/*! Number of TX packets seen through driverlink */
	u64 dl_tx_packets;
	/*! Number of TX packets seen through driverlink we didn't like */
	u64 dl_tx_bad_packets;
	/*! Number of RX packets seen through driverlink */
	u64 dl_rx_packets;
	/*! Number of mac addresses we are forwarding to */
	u32 num_fwds;
};

/*! Debug fs entries for each of the above stats */
struct netback_accel_global_dbfs {
	struct dentry *dl_tx_packets;
	struct dentry *dl_tx_bad_packets;
	struct dentry *dl_rx_packets;
	struct dentry *num_fwds;
};

#if NETBACK_ACCEL_STATS
extern struct netback_accel_global_stats global_stats;
#endif

/*! \brief Initialise the debugfs root and populate with global stats */
extern void netback_accel_debugfs_init(void);

/*! \brief Remove our debugfs root directory */
extern void netback_accel_debugfs_fini(void);

/*! \brief Add per-bend statistics to debug fs */
extern int netback_accel_debugfs_create(struct netback_accel *bend);
/*! \brief Remove per-bend statistics from debug fs */
extern int netback_accel_debugfs_remove(struct netback_accel *bend);

#endif /* NETBACK_ACCEL_H */


