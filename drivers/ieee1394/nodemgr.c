/*
 * Node information (ConfigROM) collection and management.
 *
 * Copyright (C) 2000 Andreas E. Bombe
 *               2001 Ben Collins <bcollins@debian.net>
 *
 * This code is licensed under the GPL.  See the file COPYING in the root
 * directory of the kernel sources for details.
 */

#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/slab.h>
#include <asm/byteorder.h>
#include <asm/atomic.h>
#include <linux/smp_lock.h>
#include <linux/interrupt.h>

#include "ieee1394_types.h"
#include "ieee1394.h"
#include "hosts.h"
#include "ieee1394_transactions.h"
#include "highlevel.h"
#include "csr.h"
#include "nodemgr.h"


#define NODE_BUS_FMT	"%d:%d"
#define NODE_BUS_ARGS(nodeid) \
	(nodeid & NODE_MASK), ((nodeid & BUS_MASK) >> 6)

/* Basically what we do here is start off retrieving the bus_info block.
 * From there will fill in some info about the node, verify it is of IEEE
 * 1394 type, and that the crc checks out ok. After that we start off with
 * the root directory, and subdirectories. To do this, we retrieve the
 * quadlet header for a directory, find out the length, and retrieve the
 * complete directory entry (be it a leaf or a directory). We then process
 * it and add the info to our structure for that particular node.
 *
 * We verify CRC's along the way for each directory/block/leaf. The
 * entire node structure is generic, and simply stores the information in
 * a way that's easy to parse by the protocol interface.
 *
 * XXX: Most of this isn't done yet :)  */


static LIST_HEAD(node_list);
static rwlock_t node_lock = RW_LOCK_UNLOCKED;

static LIST_HEAD(host_info_list);
static spinlock_t host_info_lock = SPIN_LOCK_UNLOCKED;

struct host_info {
	struct hpsb_host *host;
	struct tq_struct task;
	struct list_head list;
};

static struct node_entry *create_node_entry(void)
{
        struct node_entry *ne;
        unsigned long flags;

        ne = kmalloc(sizeof(struct node_entry), SLAB_ATOMIC);
        if (!ne) return NULL;

        INIT_LIST_HEAD(&ne->list);
	INIT_LIST_HEAD(&ne->unit_directories);
        ne->guid = (u64) -1;
        ne->host = NULL;
        ne->nodeid = 0;
        atomic_set(&ne->generation, -1);

        write_lock_irqsave(&node_lock, flags);
        list_add_tail(&ne->list, &node_list);
        write_unlock_irqrestore(&node_lock, flags);

        return ne;
}

static struct node_entry *find_entry_by_guid(u64 guid)
{
        struct list_head *lh;
        struct node_entry *ne;
        
        list_for_each(lh, &node_list) {
                ne = list_entry(lh, struct node_entry, list);
                if (ne->guid == guid) return ne;
        }

        return NULL;
}

static struct node_entry *find_entry_by_nodeid(nodeid_t nodeid)
{
	struct list_head *lh;
	struct node_entry *ne;

	list_for_each(lh, &node_list) {
		ne = list_entry(lh, struct node_entry, list);
		if (ne->nodeid == nodeid) return ne;
	}

	return NULL;
}

int nodemgr_read_quadlet(struct node_entry *ne, 
			 octlet_t address, quadlet_t *quad)
{
	if (hpsb_read(ne->host, ne->nodeid, address, quad, 4)) {
		HPSB_DEBUG("read of address %Lx failed", address);
		return -EAGAIN;
	}
	*quad = be32_to_cpu(*quad);

	return 0;
}

#define CONFIG_ROM_VENDOR_ID		0x03
#define CONFIG_ROM_MODEL_ID		0x17
#define CONFIG_ROM_NODE_CAPABILITES	0x0C
#define CONFIG_ROM_UNIT_DIRECTORY	0xd1
#define CONFIG_ROM_SPECIFIER_ID		0x12 
#define CONFIG_ROM_VERSION		0x13
#define CONFIG_ROM_DESCRIPTOR_LEAF	0x81
#define CONFIG_ROM_DESCRIPTOR_DIRECTORY	0xc1

/* This implementation currently only scans the config rom and its
 * immediate unit directories looking for software_id and
 * software_version entries, in order to get driver autoloading working.
 */

static void nodemgr_process_unit_directory(struct node_entry *ne, 
					   octlet_t address)
{
	struct unit_directory *ud;
	octlet_t a;
	quadlet_t quad;
	int length, i;
	

	if (!(ud = kmalloc (sizeof *ud, GFP_KERNEL)))
		goto unit_directory_error;

	memset (ud, 0, sizeof *ud);
	ud->address = address;

	if (nodemgr_read_quadlet(ne, address, &quad))
		goto unit_directory_error;
	length = quad >> 16;
	a = address + 4;

	for (i = 0; i < length; i++, a += 4) {
		int code, value;

		if (nodemgr_read_quadlet(ne, a, &quad))
			goto unit_directory_error;
		code = quad >> 24;
		value = quad & 0xffffff;

		switch (code) {
		case CONFIG_ROM_VENDOR_ID:
			ud->vendor_id = value;
			ud->flags |= UNIT_DIRECTORY_VENDOR_ID;
			break;

		case CONFIG_ROM_MODEL_ID:
			ud->model_id = value;
			ud->flags |= UNIT_DIRECTORY_MODEL_ID;
			break;

		case CONFIG_ROM_SPECIFIER_ID:
			ud->specifier_id = value;
			ud->flags |= UNIT_DIRECTORY_SPECIFIER_ID;
			break;

		case CONFIG_ROM_VERSION:
			ud->version = value;
			ud->flags |= UNIT_DIRECTORY_VERSION;
			break;

		case CONFIG_ROM_DESCRIPTOR_LEAF:
		case CONFIG_ROM_DESCRIPTOR_DIRECTORY:
			/* TODO: read strings... icons? */
			break;
		}
	}

	list_add_tail (&ud->list, &ne->unit_directories);
	return;

unit_directory_error:	
	if (ud != NULL)
		kfree(ud);
}

#ifdef CONFIG_IEEE1394_VERBOSEDEBUG
static void dump_directories (struct node_entry *ne)
{
	struct list_head *l;

	HPSB_DEBUG("vendor_id=0x%06x, capabilities=0x%06x",
		   ne->vendor_id, ne->capabilities);
	list_for_each (l, &ne->unit_directories) {
		struct unit_directory *ud = list_entry (l, struct unit_directory, list);
		HPSB_DEBUG("unit directory:");
		if (ud->flags & UNIT_DIRECTORY_VENDOR_ID)
			HPSB_DEBUG("  vendor_id=0x%06x ", ud->vendor_id);
		if (ud->flags & UNIT_DIRECTORY_MODEL_ID)
			HPSB_DEBUG("  model_id=0x%06x ", ud->model_id);
		if (ud->flags & UNIT_DIRECTORY_SPECIFIER_ID)
			HPSB_DEBUG("  specifier_id=0x%06x ", ud->specifier_id);
		if (ud->flags & UNIT_DIRECTORY_VERSION)
			HPSB_DEBUG("  version=0x%06x ", ud->version);
	}
}
#endif

static void nodemgr_process_root_directory(struct node_entry *ne)
{
	octlet_t address;
	quadlet_t quad;
	int length, i;

	address = CSR_REGISTER_BASE + CSR_CONFIG_ROM;
	
	if (nodemgr_read_quadlet(ne, address, &quad))
		return;
	address += 4 + (quad >> 24) * 4;

	if (nodemgr_read_quadlet(ne, address, &quad))
		return;
	length = quad >> 16;
	address += 4;

	for (i = 0; i < length; i++, address += 4) {
		int code, value;

		if (nodemgr_read_quadlet(ne, address, &quad))
			return;
		code = quad >> 24;
		value = quad & 0xffffff;

		switch (code) {
		case CONFIG_ROM_VENDOR_ID:
			ne->vendor_id = value;
			break;

		case CONFIG_ROM_NODE_CAPABILITES:
			ne->capabilities = value;
			break;

		case CONFIG_ROM_UNIT_DIRECTORY:
			nodemgr_process_unit_directory(ne, address + value * 4);
			break;			

		case CONFIG_ROM_DESCRIPTOR_LEAF:
		case CONFIG_ROM_DESCRIPTOR_DIRECTORY:
			/* TODO: read strings... icons? */
			break;
		}
	}
#ifdef CONFIG_IEEE1394_VERBOSEDEBUG
	dump_directories(ne);
#endif
}

static void register_node(struct hpsb_host *host, nodeid_t nodeid, u64 guid,
			  quadlet_t busoptions)
{
        struct node_entry *ne;
        unsigned long flags, new = 0;

        read_lock_irqsave(&node_lock, flags);
        ne = find_entry_by_guid(guid);
        read_unlock_irqrestore(&node_lock, flags);

	/* New entry */
	if (!ne) {
		if ((ne = create_node_entry()) == NULL)
			return;

		HPSB_DEBUG("%s added: node " NODE_BUS_FMT
			   ", GUID %02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x",
			   (host->node_id == nodeid) ? "Local host" : "Device",
			   NODE_BUS_ARGS(nodeid), ((u8 *)&guid)[0],
			   ((u8 *)&guid)[1], ((u8 *)&guid)[2], ((u8 *)&guid)[3],
			   ((u8 *)&guid)[4], ((u8 *)&guid)[5], ((u8 *)&guid)[6],
			   ((u8 *)&guid)[7]);

		ne->guid = guid;
		new = 1;
	}

	if (!new && ne->nodeid != nodeid)
		HPSB_DEBUG("Node " NODE_BUS_FMT " changed to " NODE_BUS_FMT,
			   NODE_BUS_ARGS(ne->nodeid), NODE_BUS_ARGS(nodeid));

	ne->host = host;
        ne->nodeid = nodeid;

	/* Now set the bus options. Only do all this crap if this is a new
	 * node, or if the generation number has changed.  */
	if (new || ne->busopt.generation != ((busoptions >> 6) & 0x3)) {
		ne->busopt.irmc		= (busoptions >> 31) & 1;
		ne->busopt.cmc		= (busoptions >> 30) & 1;
		ne->busopt.isc		= (busoptions >> 29) & 1;
		ne->busopt.bmc		= (busoptions >> 28) & 1;
		ne->busopt.pmc		= (busoptions >> 27) & 1;
		ne->busopt.cyc_clk_acc	= (busoptions >> 16) & 0xff;
		ne->busopt.max_rec	= 1 << (((busoptions >> 12) & 0xf) + 1);
		ne->busopt.generation	= (busoptions >> 6) & 0x3;
		ne->busopt.lnkspd	= busoptions & 0x7;

		/* Now, process the rest of the tree */
		nodemgr_process_root_directory(ne);
	}

	/* Since that's done, we can declare this record current */
	atomic_set(&ne->generation, get_hpsb_generation(host));

#ifdef CONFIG_IEEE1394_VERBOSEDEBUG
	HPSB_DEBUG("raw=0x%08x irmc=%d cmc=%d isc=%d bmc=%d pmc=%d cyc_clk_acc=%d "
		   "max_rec=%d gen=%d lspd=%d", busoptions,
		   ne->busopt.irmc, ne->busopt.cmc, ne->busopt.isc, ne->busopt.bmc,
		   ne->busopt.pmc, ne->busopt.cyc_clk_acc, ne->busopt.max_rec,
		   ne->busopt.generation, ne->busopt.lnkspd);
#endif

	return;
}

static void nodemgr_remove_node(struct node_entry *ne)
{
	HPSB_DEBUG("Device removed: node " NODE_BUS_FMT ", GUID "
		   "%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x",
		   NODE_BUS_ARGS(ne->nodeid),
		   ((u8 *)&ne->guid)[0], ((u8 *)&ne->guid)[1],
		   ((u8 *)&ne->guid)[2], ((u8 *)&ne->guid)[3],
		   ((u8 *)&ne->guid)[4], ((u8 *)&ne->guid)[5],
		   ((u8 *)&ne->guid)[6], ((u8 *)&ne->guid)[7]);

	list_del(&ne->list);
	kfree(ne);

	return;
}

/* This is where we probe the nodes for their information and provided
 * features.  */
static void nodemgr_node_probe(void *data)
{
	struct hpsb_host *host = (struct hpsb_host *)data;
        struct selfid *sid = (struct selfid *)host->topology_map;
	struct list_head *lh;
	struct node_entry *ne;
        int nodecount = host->node_count;
        nodeid_t nodeid = LOCAL_BUS;
	quadlet_t buffer[5], quad;
	octlet_t base = CSR_REGISTER_BASE + CSR_CONFIG_ROM;
	int flags;

	/* We need to detect when the ConfigROM's generation has changed,
	 * so we only update the node's info when it needs to be.  */
        for (; nodecount; nodecount--, nodeid++, sid++) {
		int retries = 3;
		int header_count;
		unsigned header_size;
		octlet_t guid;

		/* Skip extended, and non-active node's */
                while (sid->extended)
			sid++;
                if (!sid->link_active)
			continue;

		/* Just use our local ROM */
		if (nodeid == host->node_id) {
			int i;
			for (i = 0; i < 5; i++)
				buffer[i] = be32_to_cpu(host->csr.rom[i]);
			goto set_options;
		}

retry_configrom:
		
		if (!retries--) {
			HPSB_ERR("Giving up on node " NODE_BUS_FMT
				 " for ConfigROM probe, too many errors",
				 NODE_BUS_ARGS(nodeid));
			continue;
		}

		header_count = 0;
		header_size = 0;

#ifdef CONFIG_IEEE1394_VERBOSEDEBUG
		HPSB_INFO("Initiating ConfigROM request for node " NODE_BUS_FMT,
			  NODE_BUS_ARGS(nodeid));
#endif

		/* Now, P1212 says that devices should support 64byte block
		 * reads, aligned on 64byte boundaries. That doesn't seem
		 * to work though, and we are forced to doing quadlet
		 * sized reads.  */

		if (hpsb_read(host, nodeid, base, &quad, 4)) {
			HPSB_ERR("ConfigROM quadlet transaction error for node " NODE_BUS_FMT,
				 NODE_BUS_ARGS(nodeid));
			goto retry_configrom;
		}
		buffer[header_count++] = be32_to_cpu(quad);

		header_size = buffer[0] >> 24;

		if (header_size < 4) {
			HPSB_INFO("Node " NODE_BUS_FMT " has non-standard ROM format (%d quads), "
				  "cannot parse", NODE_BUS_ARGS(nodeid), header_size);
			continue;
		}

		while (header_count <= header_size && (header_count<<2) < sizeof(buffer)) {
			if (hpsb_read(host, nodeid, base + (header_count<<2), &quad, 4)) {
				HPSB_ERR("ConfigROM quadlet transaction error for " NODE_BUS_FMT,
					 NODE_BUS_ARGS(nodeid));
				goto retry_configrom;
			}
			buffer[header_count++] = be32_to_cpu(quad);
		}
set_options:
		if (buffer[1] != IEEE1394_BUSID_MAGIC) {
			/* This isn't a 1394 device */
			HPSB_ERR("Node " NODE_BUS_FMT " isn't an IEEE 1394 device",
				 NODE_BUS_ARGS(nodeid));
			continue;
		}

		guid = be64_to_cpu(((u64)buffer[3] << 32) | buffer[4]);
		register_node(host, nodeid, guid, buffer[2]);
        }

	/* Now check to see if we have any nodes that aren't referenced
	 * any longer.  */
        write_lock_irqsave(&node_lock, flags);
	list_for_each(lh, &node_list) {
		ne = list_entry(lh, struct node_entry, list);

		/* Only checking this host */
		if (ne->host != host)
			continue;

		/* If the generation didn't get updated, then either the
		 * node was removed, or it failed the above probe. Either
		 * way, we remove references to it, since they are
		 * invalid.  */
		if (atomic_read(&ne->generation) != get_hpsb_generation(host))
			nodemgr_remove_node(ne);
	}
	write_unlock_irqrestore(&node_lock, flags);

	return;
}


struct node_entry *hpsb_guid_get_entry(u64 guid)
{
        unsigned long flags;
        struct node_entry *ne;

        read_lock_irqsave(&node_lock, flags);
        ne = find_entry_by_guid(guid);
        read_unlock_irqrestore(&node_lock, flags);

        return ne;
}

struct node_entry *hpsb_nodeid_get_entry(nodeid_t nodeid)
{
	unsigned long flags;
	struct node_entry *ne;

	read_lock_irqsave(&node_lock, flags);
	ne = find_entry_by_nodeid(nodeid);
	read_unlock_irqrestore(&node_lock, flags);

	return ne;
}

struct hpsb_host *hpsb_get_host_by_ne(struct node_entry *ne)
{
        if (atomic_read(&ne->generation) != get_hpsb_generation(ne->host))
		return NULL;
        if (ne->nodeid == ne->host->node_id) return ne->host;
        return NULL;
}

int hpsb_guid_fill_packet(struct node_entry *ne, struct hpsb_packet *pkt)
{
        if (atomic_read(&ne->generation) != get_hpsb_generation(ne->host))
		return 0;

        pkt->host = ne->host;
        pkt->node_id = ne->nodeid;
        pkt->generation = atomic_read(&ne->generation);
        return 1;
}

static void nodemgr_add_host(struct hpsb_host *host)
{
	struct host_info *hi = kmalloc (sizeof (struct host_info), GFP_KERNEL);
	int flags;

	if (!hi) {
		HPSB_ERR ("Out of memory in Node Manager");
		return;
	}

	/* We simply initialize the struct here. We don't start the thread
	 * until the first bus reset.  */
	hi->host = host;
	INIT_LIST_HEAD(&hi->list);
	INIT_TQUEUE(&hi->task, nodemgr_node_probe, host);

	spin_lock_irqsave (&host_info_lock, flags);
	list_add_tail (&hi->list, &host_info_list);
	spin_unlock_irqrestore (&host_info_lock, flags);

	return;
}

static void nodemgr_host_reset(struct hpsb_host *host)
{
	struct list_head *lh;
	struct host_info *hi = NULL;
	int flags;

	spin_lock_irqsave (&host_info_lock, flags);
	list_for_each(lh, &host_info_list) {
		struct host_info *myhi = list_entry(lh, struct host_info, list);
		if (myhi->host == host) {
			hi = myhi;
			break;
		}
	}

	if (hi == NULL) {
		HPSB_ERR ("Could not process reset of non-existent host in Node Manager");
		goto done_reset_host;
	}

	schedule_task(&hi->task);

done_reset_host:
	spin_unlock_irqrestore (&host_info_lock, flags);

	return;
}

static void nodemgr_remove_host(struct hpsb_host *host)
{
	struct list_head *lh;
	struct host_info *hi = NULL;
	struct node_entry *ne;
	int flags;

	/* Make sure we have no active scans */
	flush_scheduled_tasks();

	/* First remove all node entries for this host */
	write_lock_irqsave(&node_lock, flags);
	list_for_each(lh, &node_list) {
		ne = list_entry(lh, struct node_entry, list);

		/* Only checking this host */
		if (ne->host != host)
			continue;

		nodemgr_remove_node(ne);
	}
	write_unlock_irqrestore(&node_lock, flags);

	spin_lock_irqsave (&host_info_lock, flags);
	list_for_each(lh, &host_info_list) {
		struct host_info *myhi = list_entry(lh, struct host_info, list);
		if (myhi->host == host) {
			hi = myhi;
			break;
		}
	}

	if (hi == NULL) {
		HPSB_ERR ("Could not remove non-existent host in Node Manager");
		goto done_remove_host;
	}

	list_del(&hi->list);
	kfree (hi);

done_remove_host:
	spin_unlock_irqrestore (&host_info_lock, flags);

	return;
}

static struct hpsb_highlevel_ops nodemgr_ops = {
	add_host:	nodemgr_add_host,
	host_reset:	nodemgr_host_reset,
	remove_host:	nodemgr_remove_host,
};

static struct hpsb_highlevel *hl;

void init_ieee1394_nodemgr(void)
{
        hl = hpsb_register_highlevel("Node manager", &nodemgr_ops);
        if (!hl) {
                HPSB_ERR("Out of memory during ieee1394 initialization");
        }
}

void cleanup_ieee1394_nodemgr(void)
{
        hpsb_unregister_highlevel(hl);
}
