/*
 * Copyright (C) 2000	Andreas E. Bombe
 *               2001	Ben Collins <bcollins@debian.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#ifndef _IEEE1394_NODEMGR_H
#define _IEEE1394_NODEMGR_H

/* '1' '3' '9' '4' in ASCII */
#define IEEE1394_BUSID_MAGIC	0x31333934

/* This is the start of a Node entry structure. It should be a stable API
 * for which to gather info from the Node Manager about devices attached
 * to the bus.  */
struct bus_options {
	u8	irmc;		/* Iso Resource Manager Capable */
	u8	cmc;		/* Cycle Master Capable */
	u8	isc;		/* Iso Capable */
	u8	bmc;		/* Bus Master Capable */
	u8	pmc;		/* Power Manager Capable (PNP spec) */
	u8	cyc_clk_acc;	/* Cycle clock accuracy */
	u8	generation;	/* Incremented when configrom changes */
	u8	lnkspd;		/* Link speed */
	u16	max_rec;	/* Maximum packet size node can receive */
};

#define UNIT_DIRECTORY_VENDOR_ID    0x01
#define UNIT_DIRECTORY_MODEL_ID     0x02
#define UNIT_DIRECTORY_SPECIFIER_ID 0x04
#define UNIT_DIRECTORY_VERSION      0x08

struct unit_directory {
	struct list_head list;
	octlet_t address;	/* Address of the unit directory on the node */
	u8 flags;		/* Indicates which entries were read */
	quadlet_t vendor_id;
	char *vendor_name;
	quadlet_t model_id;
	char *model_name;
	quadlet_t specifier_id;
	quadlet_t version;
};

struct node_entry {
	struct list_head list;
	u64 guid;			/* GUID of this node */
	struct hpsb_host *host;		/* Host this node is attached to */
	nodeid_t nodeid;		/* NodeID */
	struct bus_options busopt;	/* Bus Options */
	atomic_t generation;		/* Synced with hpsb generation */

	/* The following is read from the config rom */
	u32 vendor_id;
	u32 capabilities;	
	struct list_head unit_directories;
};

/*
 * Returns a node entry (which has its reference count incremented) or NULL if
 * the GUID in question is not known.  Getting a valid entry does not mean that
 * the node with this GUID is currently accessible (might be powered down).
 */
struct node_entry *hpsb_guid_get_entry(u64 guid);

/* Same as above, but use the nodeid to get an node entry. This is not
 * fool-proof by itself, since the nodeid can change.  */
struct node_entry *hpsb_nodeid_get_entry(nodeid_t nodeid);

/*
 * If the entry refers to a local host, this function will return the pointer
 * to the hpsb_host structure.  It will return NULL otherwise.  Once you have
 * established it is a local host, you can use that knowledge from then on (the
 * GUID won't wander to an external node).  */
struct hpsb_host *hpsb_get_host_by_ne(struct node_entry *ne);

/*
 * This will fill in the given, pre-initialised hpsb_packet with the current
 * information from the node entry (host, node ID, generation number).  It will
 * return false if the node owning the GUID is not accessible (and not modify the 
 * hpsb_packet) and return true otherwise.
 *
 * Note that packet sending may still fail in hpsb_send_packet if a bus reset
 * happens while you are trying to set up the packet (due to obsolete generation
 * number).  It will at least reliably fail so that you don't accidentally and
 * unknowingly send your packet to the wrong node.
 */
int hpsb_guid_fill_packet(struct node_entry *ne, struct hpsb_packet *pkt);


void init_ieee1394_nodemgr(void);
void cleanup_ieee1394_nodemgr(void);

#endif /* _IEEE1394_NODEMGR_H */
