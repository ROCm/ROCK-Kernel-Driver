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
#include <linux/kmod.h>

#include "ieee1394_types.h"
#include "ieee1394.h"
#include "hosts.h"
#include "ieee1394_transactions.h"
#include "ieee1394_hotplug.h"
#include "highlevel.h"
#include "csr.h"
#include "nodemgr.h"


/* 
 * Basically what we do here is start off retrieving the bus_info block.
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
 */

static LIST_HEAD(node_list);
static rwlock_t node_lock = RW_LOCK_UNLOCKED;

static LIST_HEAD(driver_list);
static rwlock_t driver_lock = RW_LOCK_UNLOCKED;

/* The rwlock unit_directory_lock is always held when manipulating the
 * global unit_directory_list, but this also protects access to the
 * lists of unit directories stored in the protocol drivers.
 */
static LIST_HEAD(unit_directory_list);
static rwlock_t unit_directory_lock = RW_LOCK_UNLOCKED;

static LIST_HEAD(host_info_list);
static spinlock_t host_info_lock = SPIN_LOCK_UNLOCKED;

struct host_info {
	struct hpsb_host *host;
	struct tq_struct task;
	struct list_head list;
};

static void nodemgr_process_config_rom(struct node_entry *ne, 
				       quadlet_t busoptions);


static struct node_entry *nodemgr_create_node(octlet_t guid, quadlet_t busoptions,
					      struct hpsb_host *host, nodeid_t nodeid)
{
        struct node_entry *ne;
        unsigned long flags;

        ne = kmalloc(sizeof(struct node_entry), SLAB_ATOMIC);
        if (!ne) return NULL;

        INIT_LIST_HEAD(&ne->list);
	INIT_LIST_HEAD(&ne->unit_directories);
        ne->host = host;
        ne->nodeid = nodeid;
        ne->guid = guid;
	atomic_set(&ne->generation, get_hpsb_generation(ne->host));

        write_lock_irqsave(&node_lock, flags);
        list_add_tail(&ne->list, &node_list);
        write_unlock_irqrestore(&node_lock, flags);

	nodemgr_process_config_rom (ne, busoptions);

	HPSB_DEBUG("%s added: node " NODE_BUS_FMT ", GUID %016Lx",
		   (host->node_id == nodeid) ? "Local host" : "Device",
		   NODE_BUS_ARGS(nodeid), (unsigned long long)guid);

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
	int i;
	int ret = 0;

	for (i = 0; i < 3; i++) {
		ret = hpsb_read(ne->host, ne->nodeid, address, quad, 4);
		if (ret != -EAGAIN)
			break;
	}
	*quad = be32_to_cpu(*quad);

	return ret;
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
	ud->ne = ne;
	ud->address = address;
	ud->arb_count = 0;

	if (nodemgr_read_quadlet(ne, address, &quad))
		goto unit_directory_error;
	length = quad >> 16;
	a = address + 4;

	for (i = 0; i < length; i++, a += 4) {
		int code;
		quadlet_t value;

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

		default:
			if (ud->arb_count < 16) {
				/* Place them in the arbitrary pairs */
				ud->arb_keys[ud->arb_count] = code;
				ud->arb_values[ud->arb_count] = value;
				ud->arb_count++;
			}
		}
	}

	list_add_tail(&ud->node_list, &ne->unit_directories);
	list_add_tail(&ud->driver_list, &unit_directory_list);

	return;

unit_directory_error:	
	if (ud != NULL)
		kfree(ud);
}

static void dump_directories (struct node_entry *ne)
{
#ifdef CONFIG_IEEE1394_VERBOSEDEBUG
	struct list_head *l;

	HPSB_DEBUG("vendor_id=0x%06x, capabilities=0x%06x",
		   ne->vendor_id, ne->capabilities);
	list_for_each (l, &ne->unit_directories) {
		struct unit_directory *ud = list_entry (l, struct unit_directory, node_list);
		HPSB_DEBUG("unit directory:");
		if (ud->flags & UNIT_DIRECTORY_VENDOR_ID)
			HPSB_DEBUG("  vendor_id=0x%06x ", ud->vendor_id);
		if (ud->flags & UNIT_DIRECTORY_MODEL_ID)
			HPSB_DEBUG("  model_id=0x%06x ", ud->model_id);
		if (ud->flags & UNIT_DIRECTORY_SPECIFIER_ID)
			HPSB_DEBUG("  sw_specifier_id=0x%06x ", ud->specifier_id);
		if (ud->flags & UNIT_DIRECTORY_VERSION)
			HPSB_DEBUG("  sw_version=0x%06x ", ud->version);
	}
#else
	return;
#endif
}

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

	dump_directories(ne);
}

#ifdef CONFIG_HOTPLUG

static void nodemgr_call_policy(char *verb, struct unit_directory *ud)
{
	char *argv [3], **envp, *buf, *scratch;
	int i = 0, value;

	if (!hotplug_path [0])
		return;
	if (!current->fs->root)
		return;
	if (!(envp = (char **) kmalloc(20 * sizeof (char *), GFP_KERNEL))) {
		HPSB_DEBUG ("ENOMEM");
		return;
	}
	if (!(buf = kmalloc(256, GFP_KERNEL))) {
		kfree(envp);
		HPSB_DEBUG("ENOMEM2");
		return;
	}

	/* only one standardized param to hotplug command: type */
	argv[0] = hotplug_path;
	argv[1] = "ieee1394";
	argv[2] = 0;

	/* minimal command environment */
	envp[i++] = "HOME=/";
	envp[i++] = "PATH=/sbin:/bin:/usr/sbin:/usr/bin";

#ifdef CONFIG_IEEE1394_VERBOSEDEBUG
	/* hint that policy agent should enter no-stdout debug mode */
	envp[i++] = "DEBUG=kernel";
#endif
	/* extensible set of named bus-specific parameters,
	 * supporting multiple driver selection algorithms.
	 */
	scratch = buf;

	envp[i++] = scratch;
	scratch += sprintf(scratch, "ACTION=%s", verb) + 1;
	envp[i++] = scratch;
	scratch += sprintf(scratch, "VENDOR_ID=%06x", ud->ne->vendor_id) + 1;
	envp[i++] = scratch;
	scratch += sprintf(scratch, "GUID=%016Lx", (long long unsigned)ud->ne->guid) + 1;
	envp[i++] = scratch;
	scratch += sprintf(scratch, "SPECIFIER_ID=%06x", ud->specifier_id) + 1;
	envp[i++] = scratch;
	scratch += sprintf(scratch, "VERSION=%06x", ud->version) + 1;
	envp[i++] = 0;

	/* NOTE: user mode daemons can call the agents too */
#ifdef CONFIG_IEEE1394_VERBOSEDEBUG
	HPSB_DEBUG("NodeMgr: %s %s %016Lx", argv[0], verb, (long long unsigned)ud->ne->guid);
#endif
	value = call_usermodehelper(argv[0], argv, envp);
	kfree(buf);
	kfree(envp);
	if (value != 0)
		HPSB_DEBUG("NodeMgr: hotplug policy returned 0x%x", value);
}

#else

static inline void
nodemgr_call_policy(char *verb, struct unit_directory *ud)
{
#ifdef CONFIG_IEEE1394_VERBOSEDEBUG
	HPSB_DEBUG("NodeMgr: nodemgr_call_policy(): hotplug not enabled");
#else
	return;
#endif
} 

#endif /* CONFIG_HOTPLUG */

static void nodemgr_claim_unit_directory(struct unit_directory *ud,
					 struct hpsb_protocol_driver *driver)
{
	ud->driver = driver;
	list_del(&ud->driver_list);
	list_add_tail(&ud->driver_list, &driver->unit_directories);
}

static void nodemgr_release_unit_directory(struct unit_directory *ud)
{
	ud->driver = NULL;
	list_del(&ud->driver_list);
	list_add_tail(&ud->driver_list, &unit_directory_list);
}

void hpsb_release_unit_directory(struct unit_directory *ud)
{
	unsigned long flags;

	write_lock_irqsave(&unit_directory_lock, flags);
	nodemgr_release_unit_directory(ud);
	write_unlock_irqrestore(&unit_directory_lock, flags);
}

static void nodemgr_free_unit_directories(struct node_entry *ne)
{
	struct list_head *lh;
	struct unit_directory *ud;

	lh = ne->unit_directories.next;
	while (lh != &ne->unit_directories) {
		ud = list_entry(lh, struct unit_directory, node_list);
		lh = lh->next;
		if (ud->driver && ud->driver->disconnect)
			ud->driver->disconnect(ud);
		nodemgr_release_unit_directory(ud);
		nodemgr_call_policy("remove", ud);
		list_del(&ud->driver_list);
		kfree(ud);
	}
}

static struct ieee1394_device_id *
nodemgr_match_driver(struct hpsb_protocol_driver *driver, 
		     struct unit_directory *ud)
{
	struct ieee1394_device_id *id;

	for (id = driver->id_table; id->match_flags != 0; id++) {
		if ((id->match_flags & IEEE1394_MATCH_VENDOR_ID) &&
		    id->vendor_id != ud->vendor_id)
			continue;

		if ((id->match_flags & IEEE1394_MATCH_MODEL_ID) &&
		    id->model_id != ud->model_id)
			continue;

		if ((id->match_flags & IEEE1394_MATCH_SPECIFIER_ID) &&
		    id->specifier_id != ud->specifier_id)
			continue;

		if ((id->match_flags & IEEE1394_MATCH_VERSION) &&
		    id->version != ud->version)
			continue;

		return id;
	}

	return NULL;
}

static struct hpsb_protocol_driver *
nodemgr_find_driver(struct unit_directory *ud)
{
	struct list_head *l;
	struct hpsb_protocol_driver *match, *driver;
	struct ieee1394_device_id *device_id;

	match = NULL;
	list_for_each(l, &driver_list) {
		driver = list_entry(l, struct hpsb_protocol_driver, list);
		device_id = nodemgr_match_driver(driver, ud);

		if (device_id != NULL) {
			match = driver;
			break;
		}
	}

	return match;
}

static void nodemgr_bind_drivers (struct node_entry *ne)
{
	struct list_head *lh;
	struct hpsb_protocol_driver *driver;
	struct unit_directory *ud;

	list_for_each(lh, &ne->unit_directories) {
		ud = list_entry(lh, struct unit_directory, node_list);
		driver = nodemgr_find_driver(ud);
		if (driver != NULL && driver->probe(ud) == 0)
			nodemgr_claim_unit_directory(ud, driver);
		nodemgr_call_policy("add", ud);
	}
}

int hpsb_register_protocol(struct hpsb_protocol_driver *driver)
{
	struct unit_directory *ud;
	struct list_head *lh;
	unsigned long flags;

        write_lock_irqsave(&driver_lock, flags);
	list_add_tail(&driver->list, &driver_list);
	write_unlock_irqrestore(&driver_lock, flags);

	write_lock_irqsave(&unit_directory_lock, flags);
	INIT_LIST_HEAD(&driver->unit_directories);
	lh = unit_directory_list.next;
	while (lh != &unit_directory_list) {
		ud = list_entry(lh, struct unit_directory, driver_list);
		lh = lh->next;
		if (nodemgr_match_driver(driver, ud) && driver->probe(ud) == 0)
			nodemgr_claim_unit_directory(ud, driver);
	}
	write_unlock_irqrestore(&unit_directory_lock, flags);

	/*
	 * Right now registration always succeeds, but maybe we should
	 * detect clashes in protocols handled by other drivers.
	 */

	return 0;
}

void hpsb_unregister_protocol(struct hpsb_protocol_driver *driver)
{
	struct list_head *lh;
	struct unit_directory *ud;
	unsigned long flags;

        write_lock_irqsave(&driver_lock, flags);
	list_del(&driver->list);
	write_unlock_irqrestore(&driver_lock, flags);

	write_lock_irqsave(&unit_directory_lock, flags);
	lh = driver->unit_directories.next;
	while (lh != &driver->unit_directories) {
		ud = list_entry(lh, struct unit_directory, driver_list);
		lh = lh->next;
		if (ud->driver && ud->driver->disconnect)
			ud->driver->disconnect(ud);
		nodemgr_release_unit_directory(ud);
	}
	write_unlock_irqrestore(&unit_directory_lock, flags);
}

static void nodemgr_process_config_rom(struct node_entry *ne, 
				       quadlet_t busoptions)
{
	unsigned long flags;

	ne->busopt.irmc		= (busoptions >> 31) & 1;
	ne->busopt.cmc		= (busoptions >> 30) & 1;
	ne->busopt.isc		= (busoptions >> 29) & 1;
	ne->busopt.bmc		= (busoptions >> 28) & 1;
	ne->busopt.pmc		= (busoptions >> 27) & 1;
	ne->busopt.cyc_clk_acc	= (busoptions >> 16) & 0xff;
	ne->busopt.max_rec	= 1 << (((busoptions >> 12) & 0xf) + 1);
	ne->busopt.generation	= (busoptions >> 4) & 0xf;
	ne->busopt.lnkspd	= busoptions & 0x7;

#ifdef CONFIG_IEEE1394_VERBOSEDEBUG
	HPSB_DEBUG("NodeMgr: raw=0x%08x irmc=%d cmc=%d isc=%d bmc=%d pmc=%d "
		   "cyc_clk_acc=%d max_rec=%d gen=%d lspd=%d",
		   busoptions, ne->busopt.irmc, ne->busopt.cmc,
		   ne->busopt.isc, ne->busopt.bmc, ne->busopt.pmc,
		   ne->busopt.cyc_clk_acc, ne->busopt.max_rec,
		   ne->busopt.generation, ne->busopt.lnkspd);
#endif

	/*
	 * When the config rom changes we disconnect all drivers and
	 * free the cached unit directories and reread the whole
	 * thing.  If this was a new device, the call to
	 * nodemgr_disconnect_drivers is a no-op and all is well.
	 */
	write_lock_irqsave(&unit_directory_lock, flags);
	nodemgr_free_unit_directories(ne);
	nodemgr_process_root_directory(ne);
	nodemgr_bind_drivers(ne);
	write_unlock_irqrestore(&unit_directory_lock, flags);
}

/*
 * This function updates nodes that were present on the bus before the
 * reset and still are after the reset.  The nodeid and the config rom
 * may have changed, and the drivers managing this device must be
 * informed that this device just went through a bus reset, to allow
 * the to take whatever actions required.
 */
static void nodemgr_update_node(struct node_entry *ne, quadlet_t busoptions,
                               struct hpsb_host *host, nodeid_t nodeid)
{
	struct list_head *lh;

	if (ne->nodeid != nodeid)
		HPSB_DEBUG("Node " NODE_BUS_FMT " changed to " NODE_BUS_FMT,
			   NODE_BUS_ARGS(ne->nodeid), NODE_BUS_ARGS(nodeid));

	ne->host = host;
	ne->nodeid = nodeid;

	if (ne->busopt.generation != ((busoptions >> 4) & 0xf))
		nodemgr_process_config_rom (ne, busoptions);

	/* Since that's done, we can declare this record current */
	atomic_set(&ne->generation, get_hpsb_generation(ne->host));

	list_for_each (lh, &ne->unit_directories) {
		struct unit_directory *ud;

		ud = list_entry (lh, struct unit_directory, node_list);
		if (ud->driver != NULL && ud->driver->update != NULL)
			ud->driver->update(ud);
	}
}

static int read_businfo_block(struct hpsb_host *host, nodeid_t nodeid,
			      quadlet_t *buffer, int buffer_length)
{
	octlet_t base = CSR_REGISTER_BASE + CSR_CONFIG_ROM;
	int retries = 3;
	int header_count;
	unsigned header_size;
	quadlet_t quad;

retry_configrom:

	if (!retries--) {
		HPSB_ERR("Giving up on node " NODE_BUS_FMT
			 " for ConfigROM probe, too many errors",
			 NODE_BUS_ARGS(nodeid));
		return -1;
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
		return -1;
	}

	while (header_count <= header_size && header_count < buffer_length) {
		if (hpsb_read(host, nodeid, base + (header_count<<2), &quad, 4)) {
			HPSB_ERR("ConfigROM quadlet transaction error for " NODE_BUS_FMT,
				 NODE_BUS_ARGS(nodeid));
			goto retry_configrom;
		}
		buffer[header_count++] = be32_to_cpu(quad);
	}

	return 0;
}

static void nodemgr_remove_node(struct node_entry *ne)
{
	unsigned long flags;

	HPSB_DEBUG("Device removed: node " NODE_BUS_FMT ", GUID %016Lx",
		   NODE_BUS_ARGS(ne->nodeid), (unsigned long long)ne->guid);

	write_lock_irqsave(&unit_directory_lock, flags);
	nodemgr_free_unit_directories(ne);
	write_unlock_irqrestore(&unit_directory_lock, flags);
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
	struct list_head *lh, *next;
	struct node_entry *ne;
        int nodecount = host->node_count;
        nodeid_t nodeid = LOCAL_BUS;
	quadlet_t buffer[5];
	octlet_t guid;
	unsigned long flags;

	/* We need to detect when the ConfigROM's generation has changed,
	 * so we only update the node's info when it needs to be.  */
        for (; nodecount; nodecount--, nodeid++, sid++) {
		/* Skip extended, and non-active node's */
                while (sid->extended)
			sid++;
                if (!sid->link_active)
			continue;

		if (read_businfo_block (host, nodeid, buffer, sizeof(buffer) >> 2))
			continue;

		if (buffer[1] != IEEE1394_BUSID_MAGIC) {
			/* This isn't a 1394 device */
			HPSB_ERR("Node " NODE_BUS_FMT " isn't an IEEE 1394 device",
				 NODE_BUS_ARGS(nodeid));
			continue;
		}

		guid = ((u64)buffer[3] << 32) | buffer[4];
		ne = hpsb_guid_get_entry(guid);

		if (!ne)
			nodemgr_create_node(guid, buffer[2], host, nodeid);
		else
			nodemgr_update_node(ne, buffer[2], host, nodeid);
        }

	/* Now check to see if we have any nodes that aren't referenced
	 * any longer.  */
        write_lock_irqsave(&node_lock, flags);
	for (lh = node_list.next; lh != &node_list; lh = next) {
		ne = list_entry(lh, struct node_entry, list);
		next = lh->next;

		/* Only checking this host */
		if (ne->host != host)
			continue;

		/* If the generation didn't get updated, then either the
		 * node was removed, or it failed the above probe. Either
		 * way, we remove references to it, since they are
		 * invalid.  */
		if (!hpsb_node_entry_valid(ne))
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
	unsigned long flags;

	if (!hi) {
		HPSB_ERR ("NodeMgr: out of memory in add host");
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
	unsigned long flags;

	spin_lock_irqsave (&host_info_lock, flags);
	list_for_each(lh, &host_info_list) {
		struct host_info *myhi = list_entry(lh, struct host_info, list);
		if (myhi->host == host) {
			hi = myhi;
			break;
		}
	}

	if (hi == NULL) {
		HPSB_ERR ("NodeMgr: could not process reset of non-existent host");
		goto done_reset_host;
	}

	schedule_task(&hi->task);

done_reset_host:
	spin_unlock_irqrestore (&host_info_lock, flags);

	return;
}

static void nodemgr_remove_host(struct hpsb_host *host)
{
	struct list_head *lh, *next;
	struct node_entry *ne;
	unsigned long flags;

	/* Make sure we have no active scans */
	flush_scheduled_tasks();

	/* First remove all node entries for this host */
	write_lock_irqsave(&node_lock, flags);

	for (lh = node_list.next; lh != &node_list; lh = next) {
		ne = list_entry(lh, struct node_entry, list);
		next = lh->next;

		/* Only checking this host */
		if (ne->host != host)
			continue;

		nodemgr_remove_node(ne);
	}
	write_unlock_irqrestore(&node_lock, flags);

	spin_lock_irqsave (&host_info_lock, flags);
	list_for_each(lh, &host_info_list) {
		struct host_info *hi = list_entry(lh, struct host_info, list);
		if (hi->host == host) {
			list_del(&hi->list);
			kfree (hi);
			break;
		}
	}

	if (lh == host_info_list.next)
		HPSB_ERR ("NodeMgr: could not remove non-existent host");

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
		HPSB_ERR("NodeMgr: out of memory during ieee1394 initialization");
        }
}

void cleanup_ieee1394_nodemgr(void)
{
        hpsb_unregister_highlevel(hl);
}
