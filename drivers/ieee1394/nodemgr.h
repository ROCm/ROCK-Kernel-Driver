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

#include <linux/device.h>
#include "ieee1394_core.h"
#include "ieee1394_hotplug.h"

#define CONFIG_ROM_BUS_INFO_LENGTH(q)		((q) >> 24)
#define CONFIG_ROM_BUS_CRC_LENGTH(q)		(((q) >> 16) & 0xff)
#define CONFIG_ROM_BUS_CRC(q)			((q) & 0xffff)

#define CONFIG_ROM_ROOT_LENGTH(q)		((q) >> 16)
#define CONFIG_ROM_ROOT_CRC(q)			((q) & 0xffff)

#define CONFIG_ROM_DIRECTORY_LENGTH(q)		((q) >> 16)
#define CONFIG_ROM_DIRECTORY_CRC(q)		((q) & 0xffff)

#define CONFIG_ROM_LEAF_LENGTH(q)		((q) >> 16)
#define CONFIG_ROM_LEAF_CRC(q)			((q) & 0xffff)

#define CONFIG_ROM_DESCRIPTOR_TYPE(q)		((q) >> 24)
#define CONFIG_ROM_DESCRIPTOR_SPECIFIER_ID(q)	((q) & 0xffffff)
#define CONFIG_ROM_DESCRIPTOR_WIDTH(q)		((q) >> 28)
#define CONFIG_ROM_DESCRIPTOR_CHAR_SET(q)	(((q) >> 16) & 0xfff)
#define CONFIG_ROM_DESCRIPTOR_LANG(q)		((q) & 0xffff)

#define CONFIG_ROM_KEY_ID_MASK			0x3f
#define CONFIG_ROM_KEY_TYPE_MASK		0xc0
#define CONFIG_ROM_KEY_TYPE_IMMEDIATE		0x00
#define CONFIG_ROM_KEY_TYPE_OFFSET		0x40
#define CONFIG_ROM_KEY_TYPE_LEAF		0x80
#define CONFIG_ROM_KEY_TYPE_DIRECTORY		0xc0

#define CONFIG_ROM_KEY(q)			((q) >> 24)
#define CONFIG_ROM_VALUE(q)			((q) & 0xffffff)

#define CONFIG_ROM_VENDOR_ID			0x03
#define CONFIG_ROM_MODEL_ID			0x17
#define CONFIG_ROM_NODE_CAPABILITES		0x0C
#define CONFIG_ROM_UNIT_DIRECTORY		0xd1
#define CONFIG_ROM_LOGICAL_UNIT_DIRECTORY	0xd4
#define CONFIG_ROM_SPECIFIER_ID			0x12 
#define CONFIG_ROM_UNIT_SW_VERSION		0x13
#define CONFIG_ROM_DESCRIPTOR_LEAF		0x81
#define CONFIG_ROM_DESCRIPTOR_DIRECTORY		0xc1

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


#define UNIT_DIRECTORY_VENDOR_ID		0x01
#define UNIT_DIRECTORY_MODEL_ID			0x02
#define UNIT_DIRECTORY_SPECIFIER_ID		0x04
#define UNIT_DIRECTORY_VERSION			0x08
#define UNIT_DIRECTORY_VENDOR_TEXT		0x10
#define UNIT_DIRECTORY_MODEL_TEXT		0x20
#define UNIT_DIRECTORY_HAS_LUN_DIRECTORY	0x40
#define UNIT_DIRECTORY_LUN_DIRECTORY		0x80

/*
 * A unit directory corresponds to a protocol supported by the
 * node. If a node supports eg. IP/1394 and AV/C, its config rom has a
 * unit directory for each of these protocols.
 */
struct unit_directory {
	struct node_entry *ne;  /* The node which this directory belongs to */
	octlet_t address;	/* Address of the unit directory on the node */
	u8 flags;		/* Indicates which entries were read */

	quadlet_t vendor_id;
	const char *vendor_name;
	const char *vendor_oui;

	int vendor_name_size;
	quadlet_t model_id;
	const char *model_name;
	int model_name_size;
	quadlet_t specifier_id;
	quadlet_t version;

	unsigned int id;

	int length;		/* Number of quadlets */

	struct device device;

	/* XXX Must be last in the struct! */
	quadlet_t quadlets[0];
};

struct node_entry {
	u64 guid;			/* GUID of this node */
	u32 guid_vendor_id;		/* Top 24bits of guid */
	const char *guid_vendor_oui;	/* OUI name of guid vendor id */

	struct hpsb_host *host;		/* Host this node is attached to */
	nodeid_t nodeid;		/* NodeID */
	struct bus_options busopt;	/* Bus Options */
	int needs_probe;
	unsigned int generation;	/* Synced with hpsb generation */

	/* The following is read from the config rom */
	u32 vendor_id;
	const char *vendor_name;
	const char *vendor_oui;

	u32 capabilities;
	struct hpsb_tlabel_pool *tpool;

	struct device device;

	/* XXX Must be last in the struct! */
	quadlet_t quadlets[0];
};

struct hpsb_protocol_driver {
	/* The name of the driver, e.g. SBP2 or IP1394 */
	const char *name;

	/*
	 * The device id table describing the protocols and/or devices
	 * supported by this driver.  This is used by the nodemgr to
	 * decide if a driver could support a given node, but the
	 * probe function below can implement further protocol
	 * dependent or vendor dependent checking.
	 */
	struct ieee1394_device_id *id_table;

	/*
	 * The update function is called when the node has just
	 * survived a bus reset, i.e. it is still present on the bus.
	 * However, it may be necessary to reestablish the connection
	 * or login into the node again, depending on the protocol.
	 */
	void (*update)(struct unit_directory *ud);

	/* Our LDM structure */
	struct device_driver driver;
};

int hpsb_register_protocol(struct hpsb_protocol_driver *driver);
void hpsb_unregister_protocol(struct hpsb_protocol_driver *driver);

static inline int hpsb_node_entry_valid(struct node_entry *ne)
{
	return ne->generation == get_hpsb_generation(ne->host);
}

/*
 * Returns a node entry (which has its reference count incremented) or NULL if
 * the GUID in question is not known.  Getting a valid entry does not mean that
 * the node with this GUID is currently accessible (might be powered down).
 */
struct node_entry *hpsb_guid_get_entry(u64 guid);

/* Same as above, but use the nodeid to get an node entry. This is not
 * fool-proof by itself, since the nodeid can change.  */
struct node_entry *hpsb_nodeid_get_entry(struct hpsb_host *host, nodeid_t nodeid);

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
void hpsb_node_fill_packet(struct node_entry *ne, struct hpsb_packet *pkt);

int hpsb_node_read(struct node_entry *ne, u64 addr,
		   quadlet_t *buffer, size_t length);
int hpsb_node_write(struct node_entry *ne, u64 addr, 
		    quadlet_t *buffer, size_t length);
int hpsb_node_lock(struct node_entry *ne, u64 addr, 
		   int extcode, quadlet_t *data, quadlet_t arg);


void init_ieee1394_nodemgr(void);
void cleanup_ieee1394_nodemgr(void);

#endif /* _IEEE1394_NODEMGR_H */
