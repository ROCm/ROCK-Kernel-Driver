/*
 * VLAN		An implementation of 802.1Q VLAN tagging.
 *
 * Authors:	Ben Greear <greearb@candelatech.com>
 *
 *		This program is free software; you can redistribute it and/or
 *		modify it under the terms of the GNU General Public License
 *		as published by the Free Software Foundation; either version
 *		2 of the License, or (at your option) any later version.
 *
 */

#ifndef _LINUX_IF_VLAN_H_
#define _LINUX_IF_VLAN_H_

#ifdef __KERNEL__

/* externally defined structs */
struct vlan_group;
struct net_device;
struct sk_buff;
struct packet_type;
struct vlan_collection;
struct vlan_dev_info;

#include <linux/proc_fs.h> /* for proc_dir_entry */
#include <linux/netdevice.h>

#define VLAN_HLEN	4		/* The additional bytes (on top of the Ethernet header)
					 * that VLAN requires.
					 */
#define VLAN_ETH_ALEN	6		/* Octets in one ethernet addr	 */
#define VLAN_ETH_HLEN	18		/* Total octets in header.	 */
#define VLAN_ETH_ZLEN	64		/* Min. octets in frame sans FCS */

/*
 * According to 802.3ac, the packet can be 4 bytes longer. --Klika Jan
 */
#define VLAN_ETH_DATA_LEN	1500	/* Max. octets in payload	 */
#define VLAN_ETH_FRAME_LEN	1518	/* Max. octets in frame sans FCS */

struct vlan_ethhdr {
   unsigned char	h_dest[ETH_ALEN];	   /* destination eth addr	*/
   unsigned char	h_source[ETH_ALEN];	   /* source ether addr	*/
   unsigned short       h_vlan_proto;              /* Should always be 0x8100 */
   unsigned short       h_vlan_TCI;                /* Encapsulates priority and VLAN ID */
   unsigned short	h_vlan_encapsulated_proto; /* packet type ID field (or len) */
};

struct vlan_hdr {
   unsigned short       h_vlan_TCI;                /* Encapsulates priority and VLAN ID */
   unsigned short       h_vlan_encapsulated_proto; /* packet type ID field (or len) */
};

/*  Find a VLAN device by the MAC address of it's Ethernet device, and
 *  it's VLAN ID.  The default configuration is to have VLAN's scope
 *  to be box-wide, so the MAC will be ignored.  The mac will only be
 *  looked at if we are configured to have a seperate set of VLANs per
 *  each MAC addressable interface.  Note that this latter option does
 *  NOT follow the spec for VLANs, but may be useful for doing very
 *  large quantities of VLAN MUX/DEMUX onto FrameRelay or ATM PVCs.
 */
struct net_device *find_802_1Q_vlan_dev(struct net_device* real_dev,
                                        unsigned short VID); /* vlan.c */

/* found in af_inet.c */
extern int (*vlan_ioctl_hook)(unsigned long arg);

/* found in vlan_dev.c */
struct net_device_stats* vlan_dev_get_stats(struct net_device* dev);
int vlan_dev_rebuild_header(struct sk_buff *skb);
int vlan_skb_recv(struct sk_buff *skb, struct net_device *dev,
                  struct packet_type* ptype);
int vlan_dev_hard_header(struct sk_buff *skb, struct net_device *dev,
                         unsigned short type, void *daddr, void *saddr,
                         unsigned len);
int vlan_dev_hard_start_xmit(struct sk_buff *skb, struct net_device *dev);
int vlan_dev_change_mtu(struct net_device *dev, int new_mtu);
int vlan_dev_set_mac_address(struct net_device *dev, void* addr);
int vlan_dev_open(struct net_device* dev);
int vlan_dev_stop(struct net_device* dev);
int vlan_dev_init(struct net_device* dev);
void vlan_dev_destruct(struct net_device* dev);
void vlan_dev_copy_and_sum(struct sk_buff *dest, unsigned char *src,
                           int length, int base);
int vlan_dev_set_ingress_priority(char* dev_name, __u32 skb_prio, short vlan_prio);
int vlan_dev_set_egress_priority(char* dev_name, __u32 skb_prio, short vlan_prio);
int vlan_dev_set_vlan_flag(char* dev_name, __u32 flag, short flag_val);

/* VLAN multicast stuff */
/* Delete all of the MC list entries from this vlan device.  Also deals
 * with the underlying device...
 */
void vlan_flush_mc_list(struct net_device* dev);
/* copy the mc_list into the vlan_info structure. */
void vlan_copy_mc_list(struct dev_mc_list* mc_list, struct vlan_dev_info* vlan_info);
/** dmi is a single entry into a dev_mc_list, a single node.  mc_list is
 *  an entire list, and we'll iterate through it.
 */
int vlan_should_add_mc(struct dev_mc_list *dmi, struct dev_mc_list *mc_list);
/** Taken from Gleb + Lennert's VLAN code, and modified... */
void vlan_dev_set_multicast_list(struct net_device *vlan_dev);

int vlan_collection_add_vlan(struct vlan_collection* vc, unsigned short vlan_id,
                             unsigned short flags);
int vlan_collection_remove_vlan(struct vlan_collection* vc,
                                struct net_device* vlan_dev);
int vlan_collection_remove_vlan_id(struct vlan_collection* vc, unsigned short vlan_id);

/* found in vlan.c */
/* Our listing of VLAN group(s) */
extern struct vlan_group* p802_1Q_vlan_list;

#define VLAN_NAME "vlan"

/* if this changes, algorithm will have to be reworked because this
 * depends on completely exhausting the VLAN identifier space.  Thus
 * it gives constant time look-up, but it many cases it wastes memory.
 */
#define VLAN_GROUP_ARRAY_LEN 4096

struct vlan_group {
	int real_dev_ifindex; /* The ifindex of the ethernet(like) device the vlan is attached to. */
	struct net_device *vlan_devices[VLAN_GROUP_ARRAY_LEN];

	struct vlan_group *next; /* the next in the list */
};

struct vlan_priority_tci_mapping {
	unsigned long priority;
	unsigned short vlan_qos; /* This should be shifted when first set, so we only do it
				  * at provisioning time.
				  * ((skb->priority << 13) & 0xE000)
				  */
	struct vlan_priority_tci_mapping *next;
};

/* Holds information that makes sense if this device is a VLAN device. */
struct vlan_dev_info {
	/** This will be the mapping that correlates skb->priority to
	 * 3 bits of VLAN QOS tags...
	 */
	unsigned long ingress_priority_map[8];
	struct vlan_priority_tci_mapping *egress_priority_map[16]; /* hash table */

	unsigned short vlan_id;        /*  The VLAN Identifier for this interface. */
	unsigned short flags;          /* (1 << 0) re_order_header   This option will cause the
                                        *   VLAN code to move around the ethernet header on
                                        *   ingress to make the skb look **exactly** like it
                                        *   came in from an ethernet port.  This destroys some of
                                        *   the VLAN information in the skb, but it fixes programs
                                        *   like DHCP that use packet-filtering and don't understand
                                        *   802.1Q
                                        */
	struct dev_mc_list *old_mc_list;  /* old multi-cast list for the VLAN interface..
                                           * we save this so we can tell what changes were
                                           * made, in order to feed the right changes down
                                           * to the real hardware...
                                           */
	int old_allmulti;               /* similar to above. */
	int old_promiscuity;            /* similar to above. */
	struct net_device *real_dev;    /* the underlying device/interface */
	struct proc_dir_entry *dent;    /* Holds the proc data */
	unsigned long cnt_inc_headroom_on_tx; /* How many times did we have to grow the skb on TX. */
	unsigned long cnt_encap_on_xmit;      /* How many times did we have to encapsulate the skb on TX. */
	struct net_device_stats dev_stats; /* Device stats (rx-bytes, tx-pkts, etc...) */
};

#define VLAN_DEV_INFO(x) ((struct vlan_dev_info *)(x->priv))

/* inline functions */

/* Used in vlan_skb_recv */
static inline struct sk_buff *vlan_check_reorder_header(struct sk_buff *skb)
{
	if (VLAN_DEV_INFO(skb->dev)->flags & 1) {
		skb = skb_share_check(skb, GFP_ATOMIC);
		if (skb) {
			/* Lifted from Gleb's VLAN code... */
			memmove(skb->data - ETH_HLEN,
				skb->data - VLAN_ETH_HLEN, 12);
			skb->mac.raw += VLAN_HLEN;
		}
	}

	return skb;
}

static inline unsigned short vlan_dev_get_egress_qos_mask(struct net_device* dev,
							  struct sk_buff* skb)
{
	struct vlan_priority_tci_mapping *mp =
		VLAN_DEV_INFO(dev)->egress_priority_map[(skb->priority & 0xF)];

	while (mp) {
		if (mp->priority == skb->priority) {
			return mp->vlan_qos; /* This should already be shifted to mask
					      * correctly with the VLAN's TCI
					      */
		}
		mp = mp->next;
	}
	return 0;
}

static inline int vlan_dmi_equals(struct dev_mc_list *dmi1,
                                  struct dev_mc_list *dmi2)
{
	return ((dmi1->dmi_addrlen == dmi2->dmi_addrlen) &&
		(memcmp(dmi1->dmi_addr, dmi2->dmi_addr, dmi1->dmi_addrlen) == 0));
}

static inline void vlan_destroy_mc_list(struct dev_mc_list *mc_list)
{
	struct dev_mc_list *dmi = mc_list;
	struct dev_mc_list *next;

	while(dmi) {
		next = dmi->next;
		kfree(dmi);
		dmi = next;
	}
}

#endif /* __KERNEL__ */

/* VLAN IOCTLs are found in sockios.h */

/* Passed in vlan_ioctl_args structure to determine behaviour. */
enum vlan_ioctl_cmds {
	ADD_VLAN_CMD,
	DEL_VLAN_CMD,
	SET_VLAN_INGRESS_PRIORITY_CMD,
	SET_VLAN_EGRESS_PRIORITY_CMD,
	GET_VLAN_INGRESS_PRIORITY_CMD,
	GET_VLAN_EGRESS_PRIORITY_CMD,
	SET_VLAN_NAME_TYPE_CMD,
	SET_VLAN_FLAG_CMD
};

enum vlan_name_types {
	VLAN_NAME_TYPE_PLUS_VID, /* Name will look like:  vlan0005 */
	VLAN_NAME_TYPE_RAW_PLUS_VID, /* name will look like:  eth1.0005 */
	VLAN_NAME_TYPE_PLUS_VID_NO_PAD, /* Name will look like:  vlan5 */
	VLAN_NAME_TYPE_RAW_PLUS_VID_NO_PAD, /* Name will look like:  eth0.5 */
	VLAN_NAME_TYPE_HIGHEST
};

struct vlan_ioctl_args {
	int cmd; /* Should be one of the vlan_ioctl_cmds enum above. */
	char device1[24];

        union {
		char device2[24];
		int VID;
		unsigned int skb_priority;
		unsigned int name_type;
		unsigned int bind_type;
		unsigned int flag; /* Matches vlan_dev_info flags */
        } u;

	short vlan_qos;   
};

#endif /* !(_LINUX_IF_VLAN_H_) */
