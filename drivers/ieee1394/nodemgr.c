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
#include <linux/smp_lock.h>
#include <linux/interrupt.h>
#include <linux/kmod.h>
#include <linux/completion.h>
#include <linux/delay.h>
#ifdef CONFIG_PROC_FS
#include <linux/proc_fs.h>
#endif

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

/* Disables use of the hotplug calls.  */
static int nodemgr_disable_hotplug = 0;

struct host_info {
	struct hpsb_host *host;
	struct list_head list;
	struct completion exited;
	struct semaphore reset_sem;
	int pid;
};

#ifdef CONFIG_PROC_FS

#define PUTF(fmt, args...) out += sprintf(out, fmt, ## args)

static int raw1394_read_proc(char *page, char **start, off_t off,
			     int count, int *eof, void *data)
{
	struct list_head *lh;
	struct node_entry *ne;
	int len;
	char *out = page;

	list_for_each(lh, &node_list) {
		struct list_head *l;
		int ud_count = 0;

		ne = list_entry(lh, struct node_entry, list);
		if (!ne)
			continue;

		PUTF("Node[" NODE_BUS_FMT "]  GUID[%016Lx]:\n",
		     NODE_BUS_ARGS(ne->nodeid), (unsigned long long)ne->guid);

		/* Generic Node information */
		PUTF("  Vendor ID: `%s' [0x%06x]\n",
		     ne->vendor_name ?: "Unknown", ne->vendor_id);
		PUTF("  Capabilities: 0x%06x\n", ne->capabilities);
		PUTF("  Bus Options:\n");
		PUTF("    IRMC(%d) CMC(%d) ISC(%d) BMC(%d) PMC(%d) GEN(%d)\n"
		     "    LSPD(%d) MAX_REC(%d) CYC_CLK_ACC(%d)\n",
		     ne->busopt.irmc, ne->busopt.cmc, ne->busopt.isc, ne->busopt.bmc,
		     ne->busopt.pmc, ne->busopt.generation, ne->busopt.lnkspd,
		     ne->busopt.max_rec, ne->busopt.cyc_clk_acc);

		/* If this is the host entry, output some info about it aswell */
		if (ne->host != NULL && ne->host->node_id == ne->nodeid) {
			PUTF("  Host Node Status:\n");
			PUTF("    Host Driver     : %s\n", ne->host->driver->name);
			PUTF("    Nodes connected : %d\n", ne->host->node_count);
			PUTF("    Nodes active    : %d\n", ne->host->nodes_active);
			PUTF("    SelfIDs received: %d\n", ne->host->selfid_count);
			PUTF("    Irm ID          : [" NODE_BUS_FMT "]\n",
			     NODE_BUS_ARGS(ne->host->irm_id));
			PUTF("    BusMgr ID       : [" NODE_BUS_FMT "]\n",
			     NODE_BUS_ARGS(ne->host->busmgr_id));
			PUTF("    In Bus Reset    : %s\n", ne->host->in_bus_reset ? "yes" : "no");
			PUTF("    Root            : %s\n", ne->host->is_root ? "yes" : "no");
			PUTF("    Cycle Master    : %s\n", ne->host->is_cycmst ? "yes" : "no");
			PUTF("    IRM             : %s\n", ne->host->is_irm ? "yes" : "no");
			PUTF("    Bus Manager     : %s\n", ne->host->is_busmgr ? "yes" : "no");
		}

		/* Now the unit directories */
		list_for_each (l, &ne->unit_directories) {
			struct unit_directory *ud = list_entry (l, struct unit_directory, node_list);
			PUTF("  Unit Directory %d:\n", ud_count++);
			if (ud->flags & UNIT_DIRECTORY_VENDOR_ID)
				PUTF("    Vendor/Model ID: %s [%06x]",
				     ud->vendor_name ?: "Unknown", ud->vendor_id);
			else if (ud->flags & UNIT_DIRECTORY_MODEL_ID) /* Have to put something */
				PUTF("    Vendor/Model ID: %s [%06x]",
				      ne->vendor_name ?: "Unknown", ne->vendor_id);
			if (ud->flags & UNIT_DIRECTORY_MODEL_ID)
				PUTF(" / %s [%06x]", ud->model_name ?: "Unknown", ud->model_id);
			PUTF("\n");
			if (ud->flags & UNIT_DIRECTORY_SPECIFIER_ID)
				PUTF("    Software Specifier ID: %06x\n", ud->specifier_id);
			if (ud->flags & UNIT_DIRECTORY_VERSION)
				PUTF("    Software Version: %06x\n", ud->version);
			if (ud->driver)
				PUTF("    Driver: %s\n", ud->driver->name);
			PUTF("    Length (in quads): %d\n", ud->count);
		}

	}

	len = out - page;
	len -= off;
	if (len < count) {
		*eof = 1;
		if (len <= 0)
			return 0;
	} else
		len = count;

        *start = page + off;

	return len;
}

#undef PUTF
#endif /* CONFIG_PROC_FS */

static void nodemgr_process_config_rom(struct node_entry *ne, 
				       quadlet_t busoptions);

static int nodemgr_read_quadlet(struct hpsb_host *host,
				nodeid_t nodeid, unsigned int generation,
				octlet_t address, quadlet_t *quad)
{
	int i;
	int ret = 0;

	for (i = 0; i < 3; i++) {
		ret = hpsb_read(host, nodeid, generation, address, quad, 4);
		if (ret != -EAGAIN)
			break;
	}
	*quad = be32_to_cpu(*quad);

	return ret;
}

static int nodemgr_size_text_leaf(struct hpsb_host *host,
				  nodeid_t nodeid, unsigned int generation,
				  octlet_t address)
{
	quadlet_t quad;
	int size = 0;
	if (nodemgr_read_quadlet(host, nodeid, generation, address, &quad))
		return -1;
	if (CONFIG_ROM_KEY(quad) == CONFIG_ROM_DESCRIPTOR_LEAF) {
		/* This is the offset.  */
		address += 4 * CONFIG_ROM_VALUE(quad); 
		if (nodemgr_read_quadlet(host, nodeid, generation, address, &quad))
			return -1;
		/* Now we got the size of the text descriptor leaf. */
		size = CONFIG_ROM_LEAF_LENGTH(quad);
	}
	return size;
}

static int nodemgr_read_text_leaf(struct node_entry *ne,
				  octlet_t address,
				  quadlet_t *quadp)
{
	quadlet_t quad;
	int i, size, ret;

	if (nodemgr_read_quadlet(ne->host, ne->nodeid, ne->generation, address, &quad)
	    && CONFIG_ROM_KEY(quad) != CONFIG_ROM_DESCRIPTOR_LEAF)
		return -1;

	/* This is the offset.  */
	address += 4 * CONFIG_ROM_VALUE(quad);
	if (nodemgr_read_quadlet(ne->host, ne->nodeid, ne->generation, address, &quad))
		return -1;

	/* Now we got the size of the text descriptor leaf. */
	size = CONFIG_ROM_LEAF_LENGTH(quad) - 2;
	if (size <= 0)
		return -1;

	address += 4;
	for (i = 0; i < 2; i++, address += 4, quadp++) {
		if (nodemgr_read_quadlet(ne->host, ne->nodeid, ne->generation, address, quadp))
			return -1;
	}

	/* Now read the text string.  */
	ret = -ENXIO;
	for (; size > 0; size--, address += 4, quadp++) {
		for (i = 0; i < 3; i++) {
			ret = hpsb_read(ne->host, ne->nodeid, ne->generation, address, quadp, 4);
			if (ret != -EAGAIN)
				break;
		}
		if (ret)
			break;
	}

	return ret;
}

static struct node_entry *nodemgr_scan_root_directory
	(struct hpsb_host *host, nodeid_t nodeid, unsigned int generation)
{
	octlet_t address;
	quadlet_t quad;
	int length;
	int code, size, total_size;
	struct node_entry *ne;

	address = CSR_REGISTER_BASE + CSR_CONFIG_ROM;
	
	if (nodemgr_read_quadlet(host, nodeid, generation, address, &quad))
		return NULL;
	address += 4 + CONFIG_ROM_BUS_INFO_LENGTH(quad) * 4;

	if (nodemgr_read_quadlet(host, nodeid, generation, address, &quad))
		return NULL;
	length = CONFIG_ROM_ROOT_LENGTH(quad);
	address += 4;

	size = 0;
	total_size = sizeof(struct node_entry);
	for (; length > 0; length--, address += 4) {
		if (nodemgr_read_quadlet(host, nodeid, generation, address, &quad))
			return NULL;
		code = CONFIG_ROM_KEY(quad);

		if (code == CONFIG_ROM_VENDOR_ID && length > 0) {
			/* Check if there is a text descriptor leaf
			   immediately after this.  */
			size = nodemgr_size_text_leaf(host, nodeid, generation,
						      address + 4);
			if (size > 0) {
				address += 4;
				length--;
				total_size += (size + 1) * sizeof (quadlet_t);
			}
			else if (size < 0)
				return NULL;
		}
	}
	ne = kmalloc(total_size, SLAB_ATOMIC);
	if (ne != NULL) {
		if (size != 0) {
			ne->vendor_name
				= (const char *) &(ne->quadlets[2]);
			ne->quadlets[size] = 0;
		}
		else {
			ne->vendor_name = NULL;
		}
	}
	return ne; 
}

static struct node_entry *nodemgr_create_node(octlet_t guid, quadlet_t busoptions,
					      struct hpsb_host *host,
					      nodeid_t nodeid, unsigned int generation)
{
        struct node_entry *ne;
        unsigned long flags;

	ne = nodemgr_scan_root_directory (host, nodeid, generation);
        if (!ne) return NULL;

        INIT_LIST_HEAD(&ne->list);
	INIT_LIST_HEAD(&ne->unit_directories);
        ne->host = host;
        ne->nodeid = nodeid;
        ne->guid = guid;
	ne->generation = generation;

        write_lock_irqsave(&node_lock, flags);
        list_add_tail(&ne->list, &node_list);
        write_unlock_irqrestore(&node_lock, flags);

	nodemgr_process_config_rom (ne, busoptions);

	HPSB_DEBUG("%s added: Node[" NODE_BUS_FMT "]  GUID[%016Lx]  [%s]",
		   (host->node_id == nodeid) ? "Host" : "Device",
		   NODE_BUS_ARGS(nodeid), (unsigned long long)guid,
		   ne->vendor_name ?: "Unknown");

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

static struct unit_directory *nodemgr_scan_unit_directory
	(struct node_entry *ne, octlet_t address)
{
	struct unit_directory *ud;
	quadlet_t quad;
	u8 flags, todo;
	int length, size, total_size, count;
	int vendor_name_size, model_name_size;

	if (nodemgr_read_quadlet(ne->host, ne->nodeid, ne->generation, address, &quad))
		return NULL;
	length = CONFIG_ROM_DIRECTORY_LENGTH(quad) ;
	address += 4;

	size = 0;
	total_size = sizeof (struct unit_directory);
	flags = 0;
	count = 0;
	vendor_name_size = 0;
	model_name_size = 0;
	for (; length > 0; length--, address += 4) {
		int code;
		quadlet_t value;

		if (nodemgr_read_quadlet(ne->host, ne->nodeid, ne->generation,
					 address, &quad))
			return NULL;
		code = CONFIG_ROM_KEY(quad);
		value = CONFIG_ROM_VALUE(quad);

		todo = 0;
		switch (code) {
		case CONFIG_ROM_VENDOR_ID:
			todo = UNIT_DIRECTORY_VENDOR_TEXT;
			break;

		case CONFIG_ROM_MODEL_ID:
			todo = UNIT_DIRECTORY_MODEL_TEXT;
			break;

		case CONFIG_ROM_SPECIFIER_ID:
		case CONFIG_ROM_UNIT_SW_VERSION:
			break;

		case CONFIG_ROM_DESCRIPTOR_LEAF:
		case CONFIG_ROM_DESCRIPTOR_DIRECTORY:
			/* TODO: read strings... icons? */
			break;

		default:
			/* Which types of quadlets do we want to
			   store?  Only count immediate values and
			   CSR offsets for now.  */
			code &= CONFIG_ROM_KEY_TYPE_MASK;
			if ((code & 0x80) == 0)
				count++;
			break;
		}

		if (todo && length > 0) {
			/* Check if there is a text descriptor leaf
			   immediately after this.  */
			size = nodemgr_size_text_leaf(ne->host,
						      ne->nodeid,
						      ne->generation,
						      address + 4);

			if (todo == UNIT_DIRECTORY_VENDOR_TEXT)
				vendor_name_size = size;
			else
				model_name_size = size;

			if (size > 0) {
				address += 4;
				length--;
				flags |= todo;
				total_size += (size + 1) * sizeof (quadlet_t);
			}
			else if (size < 0)
				return NULL;
		}
	}
	total_size += count * sizeof (quadlet_t);
	ud = kmalloc (total_size, GFP_KERNEL);
	if (ud != NULL) {
		memset (ud, 0, sizeof *ud);
		ud->flags = flags;
		ud->count = count;
		ud->vendor_name_size = vendor_name_size;
		ud->model_name_size = model_name_size;
		/* If there is no vendor name in the unit directory,
		   use the one in the root directory.  */
		ud->vendor_name = ne->vendor_name;
	}
	return ud;
}

/* This implementation currently only scans the config rom and its
 * immediate unit directories looking for software_id and
 * software_version entries, in order to get driver autoloading working.
 */

static void nodemgr_process_unit_directory(struct node_entry *ne, 
					   octlet_t address)
{
	struct unit_directory *ud;
	quadlet_t quad;
	quadlet_t *infop;
	int length;

	if (!(ud = nodemgr_scan_unit_directory(ne, address)))
		goto unit_directory_error;

	ud->ne = ne;
	ud->address = address;

	if (nodemgr_read_quadlet(ne->host, ne->nodeid, ne->generation,
				 address, &quad))
		goto unit_directory_error;
	length = CONFIG_ROM_DIRECTORY_LENGTH(quad) ;
	address += 4;

	infop = (quadlet_t *) ud->quadlets;
	for (; length > 0; length--, address += 4, infop++) {
		int code;
		quadlet_t value;
		quadlet_t *quadp;

		if (nodemgr_read_quadlet(ne->host, ne->nodeid, ne->generation,
					 address, &quad))
			goto unit_directory_error;
		code = CONFIG_ROM_KEY(quad) ;
		value = CONFIG_ROM_VALUE(quad);

		switch (code) {
		case CONFIG_ROM_VENDOR_ID:
			ud->vendor_id = value;
			ud->flags |= UNIT_DIRECTORY_VENDOR_ID;
			if ((ud->flags & UNIT_DIRECTORY_VENDOR_TEXT) != 0) {
				length--;
				address += 4;
				quadp = &(ud->quadlets[ud->count]);
				if (nodemgr_read_text_leaf(ne, address,
							   quadp) == 0
				    && quadp[0] == 0
				    && quadp[1] == 0) {
				    	/* We only support minimal
					   ASCII and English. */
					quadp[ud->vendor_name_size] = 0;
					ud->vendor_name
						= (const char *) &(quadp[2]);
				}
			}
			break;

		case CONFIG_ROM_MODEL_ID:
			ud->model_id = value;
			ud->flags |= UNIT_DIRECTORY_MODEL_ID;
			if ((ud->flags & UNIT_DIRECTORY_MODEL_TEXT) != 0) {
				length--;
				address += 4;
				quadp = &(ud->quadlets[ud->count + ud->vendor_name_size + 1]);
				if (nodemgr_read_text_leaf(ne, address,
							   quadp) == 0
				    && quadp[0] == 0
				    && quadp[1] == 0) {
				    	/* We only support minimal
					   ASCII and English. */
					quadp[ud->model_name_size] = 0;
					ud->model_name
						= (const char *) &(quadp[2]);
				}
			}
			break;

		case CONFIG_ROM_SPECIFIER_ID:
			ud->specifier_id = value;
			ud->flags |= UNIT_DIRECTORY_SPECIFIER_ID;
			break;

		case CONFIG_ROM_UNIT_SW_VERSION:
			ud->version = value;
			ud->flags |= UNIT_DIRECTORY_VERSION;
			break;

		case CONFIG_ROM_DESCRIPTOR_LEAF:
		case CONFIG_ROM_DESCRIPTOR_DIRECTORY:
			/* TODO: read strings... icons? */
			break;

		default:
			/* Which types of quadlets do we want to
			   store?  Only count immediate values and
			   CSR offsets for now.  */
			code &= CONFIG_ROM_KEY_TYPE_MASK;
			if ((code & 0x80) == 0)
				*infop = quad;
			break;
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

	HPSB_DEBUG("vendor_id=0x%06x [%s], capabilities=0x%06x",
		   ne->vendor_id, ne->vendor_name ?: "Unknown",
		   ne->capabilities);
	list_for_each (l, &ne->unit_directories) {
		struct unit_directory *ud = list_entry (l, struct unit_directory, node_list);
		HPSB_DEBUG("unit directory:");
		if (ud->flags & UNIT_DIRECTORY_VENDOR_ID)
			HPSB_DEBUG("  vendor_id=0x%06x [%s]",
				   ud->vendor_id,
				   ud->vendor_name ?: "Unknown");
		if (ud->flags & UNIT_DIRECTORY_MODEL_ID)
			HPSB_DEBUG("  model_id=0x%06x [%s]",
				   ud->model_id,
				   ud->model_name ?: "Unknown");
		if (ud->flags & UNIT_DIRECTORY_SPECIFIER_ID)
			HPSB_DEBUG("  sw_specifier_id=0x%06x ", ud->specifier_id);
		if (ud->flags & UNIT_DIRECTORY_VERSION)
			HPSB_DEBUG("  sw_version=0x%06x ", ud->version);
	}
#endif
	return;
}

static void nodemgr_process_root_directory(struct node_entry *ne)
{
	octlet_t address;
	quadlet_t quad;
	int length;

	address = CSR_REGISTER_BASE + CSR_CONFIG_ROM;
	
	if (nodemgr_read_quadlet(ne->host, ne->nodeid, ne->generation,
				 address, &quad))
		return;
	address += 4 + CONFIG_ROM_BUS_INFO_LENGTH(quad) * 4;

	if (nodemgr_read_quadlet(ne->host, ne->nodeid, ne->generation,
				 address, &quad))
		return;
	length = CONFIG_ROM_ROOT_LENGTH(quad);
	address += 4;

	for (; length > 0; length--, address += 4) {
		int code, value;

		if (nodemgr_read_quadlet(ne->host, ne->nodeid, ne->generation,
					 address, &quad))
			return;
		code = CONFIG_ROM_KEY(quad);
		value = CONFIG_ROM_VALUE(quad);

		switch (code) {
		case CONFIG_ROM_VENDOR_ID:
			ne->vendor_id = value;
			/* Now check if there is a vendor name text
			   string.  */
			if (ne->vendor_name != NULL) {
				length--;
				address += 4;
				if (nodemgr_read_text_leaf(ne, address,
							   ne->quadlets)
				    != 0
				    || ne->quadlets [0] != 0
				    || ne->quadlets [1] != 0)
				    	/* We only support minimal
					   ASCII and English. */
					ne->vendor_name = NULL;
			}
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

	/* User requested to disable hotplug when module was loaded. */
	if (nodemgr_disable_hotplug)
		return;

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
		HPSB_DEBUG("NodeMgr: hotplug policy returned %d", value);
}

#else

static inline void
nodemgr_call_policy(char *verb, struct unit_directory *ud)
{
#ifdef CONFIG_IEEE1394_VERBOSEDEBUG
	HPSB_DEBUG("NodeMgr: nodemgr_call_policy(): hotplug not enabled");
#endif
	return;
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
                               struct hpsb_host *host,
				nodeid_t nodeid, unsigned int generation)
{
	struct list_head *lh;
	struct unit_directory *ud;

	if (ne->nodeid != nodeid) {
		HPSB_DEBUG("Node " NODE_BUS_FMT " changed to " NODE_BUS_FMT,
			   NODE_BUS_ARGS(ne->nodeid), NODE_BUS_ARGS(nodeid));
		ne->nodeid = nodeid;
	}

	if (ne->busopt.generation != ((busoptions >> 4) & 0xf))
		nodemgr_process_config_rom (ne, busoptions);

	/* Since that's done, we can declare this record current */
	ne->generation = generation;

	list_for_each (lh, &ne->unit_directories) {
		ud = list_entry (lh, struct unit_directory, node_list);
		if (ud->driver != NULL && ud->driver->update != NULL)
			ud->driver->update(ud);
	}
}

static int read_businfo_block(struct hpsb_host *host, nodeid_t nodeid, unsigned int generation,
			      quadlet_t *buffer, int buffer_length)
{
	octlet_t addr = CSR_REGISTER_BASE + CSR_CONFIG_ROM;
	unsigned header_size;
	int i;

	/* IEEE P1212 says that devices should support 64byte block
	 * reads, aligned on 64byte boundaries. That doesn't seem to
	 * work though, and we are forced to doing quadlet sized
	 * reads.  */

#ifdef CONFIG_IEEE1394_VERBOSEDEBUG
	HPSB_INFO("Initiating ConfigROM request for node " NODE_BUS_FMT,
		  NODE_BUS_ARGS(nodeid));
#endif

	if (nodemgr_read_quadlet(host, nodeid, generation,
				 addr, &buffer[0]) < 0) {
		HPSB_ERR("ConfigROM quadlet transaction error for node "
			 NODE_BUS_FMT, NODE_BUS_ARGS(nodeid));
		return -1;
	}

	header_size = buffer[0] >> 24;
	addr += 4;

	if (header_size < 4) {
		HPSB_INFO("Node " NODE_BUS_FMT " has non-standard ROM "
			  "format (%d quads), cannot parse",
			  NODE_BUS_ARGS(nodeid), header_size);
		return -1;
	}

	for (i = 1; i < buffer_length; i++, addr += 4) {
		if (nodemgr_read_quadlet(host, nodeid, generation,
					 addr, &buffer[i]) < 0) {
			HPSB_ERR("ConfigROM quadlet transaction "
				 "error for node " NODE_BUS_FMT,
				 NODE_BUS_ARGS(nodeid));
			return -1;
		}
	}

	return 0;
}		

static void nodemgr_remove_node(struct node_entry *ne)
{
	unsigned long flags;

	HPSB_DEBUG("%s removed: Node[" NODE_BUS_FMT "]  GUID[%016Lx]  [%s]",
		   (ne->host->node_id == ne->nodeid) ? "Host" : "Device",
		   NODE_BUS_ARGS(ne->nodeid), (unsigned long long)ne->guid,
		   ne->vendor_name ?: "Unknown");

	write_lock_irqsave(&unit_directory_lock, flags);
	nodemgr_free_unit_directories(ne);
	write_unlock_irqrestore(&unit_directory_lock, flags);
	list_del(&ne->list);
	kfree(ne);

	return;
}

/* This is where we probe the nodes for their information and provided
 * features.  */
static void nodemgr_node_probe_one(struct hpsb_host *host,
				   nodeid_t nodeid, int generation)
{
	struct node_entry *ne;
	quadlet_t buffer[5];
	octlet_t guid;

	/* We need to detect when the ConfigROM's generation has changed,
	 * so we only update the node's info when it needs to be.  */

	if (read_businfo_block (host, nodeid, generation,
				buffer, sizeof(buffer) >> 2))
		return;

	if (buffer[1] != IEEE1394_BUSID_MAGIC) {
		/* This isn't a 1394 device */
		HPSB_ERR("Node " NODE_BUS_FMT " isn't an IEEE 1394 device",
			 NODE_BUS_ARGS(nodeid));
		return;
	}

	guid = ((u64)buffer[3] << 32) | buffer[4];
	ne = hpsb_guid_get_entry(guid);

	if (!ne)
		nodemgr_create_node(guid, buffer[2], host, nodeid, generation);
	else
		nodemgr_update_node(ne, buffer[2], host, nodeid, generation);

	return;
}

static void nodemgr_node_probe_cleanup(struct hpsb_host *host, unsigned int generation)
{
	unsigned long flags;
	struct list_head *lh, *next;
	struct node_entry *ne;

	/* Now check to see if we have any nodes that aren't referenced
	 * any longer.  */
	write_lock_irqsave(&node_lock, flags);
	list_for_each_safe(lh, next, &node_list) {
		ne = list_entry(lh, struct node_entry, list);

		/* Only checking this host */
		if (ne->host != host)
			continue;

		/* If the generation didn't get updated, then either the
		 * node was removed, or it failed the above probe. Either
		 * way, we remove references to it, since they are
		 * invalid.  */
		if (ne->generation != generation)
			nodemgr_remove_node(ne);
	}
	write_unlock_irqrestore(&node_lock, flags);

	return;
}

static void nodemgr_node_probe(struct hpsb_host *host)
{
	int count;
	struct selfid *sid = (struct selfid *)host->topology_map;
	nodeid_t nodeid = LOCAL_BUS;
	unsigned int generation;

	/* Pause for 1 second, to make sure things settle down. If
	 * schedule_timeout returns non-zero, it means we caught a signal
	 * and need to return. */
	set_current_state(TASK_INTERRUPTIBLE);
	if (schedule_timeout (HZ))
		return;

	/* Now get the generation in which the node ID's we collect
	 * are valid.  During the bus scan we will use this generation
	 * for the read transactions, so that if another reset occurs
	 * during the scan the transactions will fail instead of
	 * returning bogus data. */
	generation = get_hpsb_generation(host);

	/* Scan each node on the bus */
	for (count = host->selfid_count; count; count--, sid++) {
		if (sid->extended)
			continue;

		if (!sid->link_active) {
			nodeid++;
			continue;
		}

		nodemgr_node_probe_one(host, nodeid++, generation);
	}

	/* If we had a bus reset while we were scanning the bus, it is
	 * possible that we did not probe all nodes.  In that case, we
	 * skip the clean up for now, since we could remove nodes that
	 * were still on the bus.  The bus reset increased
	 * hi->reset_sem, so there's a bus scan pending which will do
	 * the clean up eventually. */
	if (generation == get_hpsb_generation(host))
		nodemgr_node_probe_cleanup(host, generation);

	return;
}

static int nodemgr_host_thread(void *__hi)
{
	struct host_info *hi = (struct host_info *)__hi;

	/* No userlevel access needed */
	daemonize();

	strcpy(current->comm, "knodemgrd");

	/* Sit and wait for a signal to probe the nodes on the bus. This
	 * happens when we get a bus reset. */
	while (!down_interruptible(&hi->reset_sem))
		nodemgr_node_probe(hi->host);

#ifdef CONFIG_IEEE1394_VERBOSEDEBUG
	HPSB_DEBUG ("NodeMgr: Exiting thread for %s", hi->host->driver->name);
#endif

	complete_and_exit(&hi->exited, 0);
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

/* The following four convenience functions use a struct node_entry
 * for addressing a node on the bus.  They are intended for use by any
 * process context, not just the nodemgr thread, so we need to be a
 * little careful when reading out the node ID and generation.  The
 * thing that can go wrong is that we get the node ID, then a bus
 * reset occurs, and then we read the generation.  The node ID is
 * possibly invalid, but the generation is current, and we end up
 * sending a packet to a the wrong node.
 *
 * The solution is to make sure we read the generation first, so that
 * if a reset occurs in the process, we end up with a stale generation
 * and the transactions will fail instead of silently using wrong node
 * ID's.
 */

void hpsb_node_fill_packet(struct node_entry *ne, struct hpsb_packet *pkt)
{
        pkt->host = ne->host;
        pkt->generation = ne->generation;
	barrier();
        pkt->node_id = ne->nodeid;
}

int hpsb_node_read(struct node_entry *ne, u64 addr,
		   quadlet_t *buffer, size_t length)
{
	unsigned int generation = ne->generation;

	barrier();
	return hpsb_read(ne->host, ne->nodeid, generation,
			 addr, buffer, length);
}

int hpsb_node_write(struct node_entry *ne, u64 addr, 
		    quadlet_t *buffer, size_t length)
{
	unsigned int generation = ne->generation;

	barrier();
	return hpsb_write(ne->host, ne->nodeid, generation,
			  addr, buffer, length);
}

int hpsb_node_lock(struct node_entry *ne, u64 addr, 
		   int extcode, quadlet_t *data, quadlet_t arg)
{
	unsigned int generation = ne->generation;

	barrier();
	return hpsb_lock(ne->host, ne->nodeid, generation,
			 addr, extcode, data, arg);
}

static void nodemgr_add_host(struct hpsb_host *host)
{
	struct host_info *hi = kmalloc (sizeof (struct host_info), GFP_KERNEL);
	unsigned long flags;

	if (!hi) {
		HPSB_ERR ("NodeMgr: out of memory in add host");
		return;
	}

	/* Initialize the hostinfo here and start the thread.  The
	 * thread blocks on the reset semaphore until a bus reset
	 * happens. */
	hi->host = host;
	INIT_LIST_HEAD(&hi->list);
	init_completion(&hi->exited);
        sema_init(&hi->reset_sem, 0);

	hi->pid = kernel_thread(nodemgr_host_thread, hi,
				CLONE_FS | CLONE_FILES | CLONE_SIGHAND);
	
	if (hi->pid < 0) {
		HPSB_ERR ("NodeMgr: failed to start NodeMgr thread for %s",
			  host->driver->name);
		kfree(hi);
		return;
	}

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

#ifdef CONFIG_IEEE1394_VERBOSEDEBUG
	HPSB_DEBUG ("NodeMgr: Processing host reset for %s", host->driver->name);
#endif
	up(&hi->reset_sem);

done_reset_host:
	spin_unlock_irqrestore (&host_info_lock, flags);

	return;
}

static void nodemgr_remove_host(struct hpsb_host *host)
{
	struct list_head *lh, *next;
	struct node_entry *ne;
	unsigned long flags;
	struct host_info *hi = NULL;

	spin_lock_irqsave (&host_info_lock, flags);
	list_for_each_safe(lh, next, &host_info_list) {
		struct host_info *myhi = list_entry(lh, struct host_info, list);
		if (myhi->host == host) {
			list_del(&myhi->list);
			hi = myhi;
			break;
		}
	}

	if (!hi)
		HPSB_ERR ("NodeMgr: host %s does not exist, cannot remove",
			  host->driver->name);
	spin_unlock_irqrestore (&host_info_lock, flags);

	/* Even if we fail the host_info part, remove all the node
	 * entries.  */
	write_lock_irqsave(&node_lock, flags);
	list_for_each_safe(lh, next, &node_list) {
		ne = list_entry(lh, struct node_entry, list);

		if (ne->host == host)
			nodemgr_remove_node(ne);
	}
	write_unlock_irqrestore(&node_lock, flags);

	if (hi) {
		if (hi->pid >= 0) {
			kill_proc(hi->pid, SIGTERM, 1);
			wait_for_completion(&hi->exited);
		}
		kfree(hi);
	}

	return;
}

static struct hpsb_highlevel_ops nodemgr_ops = {
	add_host:	nodemgr_add_host,
	host_reset:	nodemgr_host_reset,
	remove_host:	nodemgr_remove_host,
};

static struct hpsb_highlevel *hl;

#define PROC_ENTRY "devices"

void init_ieee1394_nodemgr(int disable_hotplug)
{
	nodemgr_disable_hotplug = disable_hotplug;
#ifdef CONFIG_PROC_FS
	if (!create_proc_read_entry(PROC_ENTRY, 0444, ieee1394_procfs_entry, raw1394_read_proc, NULL))
		HPSB_ERR("Can't create devices procfs entry");
#endif
        hl = hpsb_register_highlevel("Node manager", &nodemgr_ops);
        if (!hl) {
		HPSB_ERR("NodeMgr: out of memory during ieee1394 initialization");
        }
}

void cleanup_ieee1394_nodemgr(void)
{
        hpsb_unregister_highlevel(hl);
#ifdef CONFIG_PROC_FS
	remove_proc_entry(PROC_ENTRY, ieee1394_procfs_entry);
#endif
}
