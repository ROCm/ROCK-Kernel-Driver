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


/* Basically what we do here is start off retrieving the bus_info block.
 * From there will fill in some info about the node, verify it is of IEEE
 * 1394 type, and the the crc checks out ok. After that we start off with
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
rwlock_t node_lock = RW_LOCK_UNLOCKED;

static LIST_HEAD(host_info_list);
spinlock_t host_info_lock = SPIN_LOCK_UNLOCKED;

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
	atomic_t changed;	/* We set this to 1 if generation has changed */
};

struct host_info {
	struct hpsb_host *host;
	pid_t pid;			/* PID of the nodemgr thread */
	pid_t ppid;			/* Parent PID for the thread */
	struct tq_struct task;		/* Used to kickstart the thread */
	wait_queue_head_t reset_wait;	/* Wait queue awoken on bus reset */
	struct list_head list;
};

struct node_entry {
        struct list_head list;
        atomic_t refcount;

        u64 guid;

        struct hpsb_host *host;
        nodeid_t node_id;

	struct bus_options busopt;

        atomic_t generation;
};

static struct node_entry *create_node_entry(void)
{
        struct node_entry *ne;
        unsigned long flags;

        ne = kmalloc(sizeof(struct node_entry), SLAB_ATOMIC);
        if (!ne) return NULL;

        INIT_LIST_HEAD(&ne->list);
        atomic_set(&ne->refcount, 0);
        ne->guid = (u64) -1;
        ne->host = NULL;
        ne->node_id = 0;
        atomic_set(&ne->generation, -1);
	atomic_set(&ne->busopt.changed, 0);

        write_lock_irqsave(&node_lock, flags);
        list_add_tail(&ne->list, &node_list);
        write_unlock_irqrestore(&node_lock, flags);

        return ne;
}

static struct node_entry *find_entry(u64 guid)
{
        struct list_head *lh;
        struct node_entry *ne;
        
        lh = node_list.next;
        while (lh != &node_list) {
                ne = list_entry(lh, struct node_entry, list);
                if (ne->guid == guid) return ne;
                lh = lh->next;
        }

        return NULL;
}

static int register_guid(struct hpsb_host *host, nodeid_t nodeid, u64 guid,
			  quadlet_t busoptions)
{
        struct node_entry *ne;
        unsigned long flags, new = 0;

        read_lock_irqsave(&node_lock, flags);
        ne = find_entry(guid);
        read_unlock_irqrestore(&node_lock, flags);

	/* New entry */
	if (!ne) {
		if ((ne = create_node_entry()) == NULL)
			return -1;

		HPSB_DEBUG("%s added: node %d, bus %d: GUID %02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x",
			   (host->node_id == nodeid) ? "Local host" : "Device",
			   nodeid & NODE_MASK, (nodeid & BUS_MASK) >> 6, ((u8 *)&guid)[0],
			   ((u8 *)&guid)[1], ((u8 *)&guid)[2], ((u8 *)&guid)[3],
			   ((u8 *)&guid)[4], ((u8 *)&guid)[5], ((u8 *)&guid)[6],
			   ((u8 *)&guid)[7]);

		ne->guid = guid;
		new = 1;
	}

	if (!new && ne->node_id != nodeid)
		HPSB_DEBUG("Node %d changed to %d on bus %d",
			   ne->node_id & NODE_MASK, nodeid & NODE_MASK, (nodeid & BUS_MASK) >> 6);

	ne->host = host;
        ne->node_id = nodeid;

        atomic_set(&ne->generation, get_hpsb_generation());

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

		new = 1; /* To make sure we probe the rest of the ConfigROM too */
	}

#ifdef CONFIG_IEEE1394_VERBOSEDEBUG
	HPSB_DEBUG("raw=0x%08x irmc=%d cmc=%d isc=%d bmc=%d pmc=%d cyc_clk_acc=%d "
		   "max_rec=%d gen=%d lspd=%d\n", busoptions,
		   ne->busopt.irmc, ne->busopt.cmc, ne->busopt.isc, ne->busopt.bmc,
		   ne->busopt.pmc, ne->busopt.cyc_clk_acc, ne->busopt.max_rec,
		   ne->busopt.generation, ne->busopt.lnkspd);
#endif

	return new;
}

static void nodemgr_remove_node(struct node_entry *ne)
{
	HPSB_DEBUG("Device removed: node %d, bus %d: GUID "
		   "%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x",
		   ne->node_id & NODE_MASK, (ne->node_id & BUS_MASK) >> 6,
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
static void nodemgr_node_probe(struct hpsb_host *host)
{
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
			HPSB_ERR("Giving up on node %d for ConfigROM probe, too many errors",
				 nodeid & NODE_MASK);
			continue;
		}

		header_count = 0;
		header_size = 0;

#ifdef CONFIG_IEEE1394_VERBOSEDEBUG
		HPSB_INFO("Initiating ConfigROM request for node %d", nodeid & NODE_MASK);
#endif

		/* Now, P1212 says that devices should support 64byte block
		 * reads, aligned on 64byte boundaries. That doesn't seem
		 * to work though, and we are forced to doing quadlet
		 * sized reads.  */

		if (hpsb_read(host, nodeid, base, &quad, 4)) {
			HPSB_ERR("ConfigROM quadlet transaction error for node %d",
				 nodeid & NODE_MASK);
			goto retry_configrom;
		}
		buffer[header_count++] = be32_to_cpu(quad);

		header_size = buffer[0] >> 24;

		if (header_size < 4) {
			HPSB_INFO("Node %d on bus %d has non-standard ROM format (%d quads), "
				  "cannot parse", nodeid & NODE_MASK, (nodeid & BUS_MASK) >> 6,
				  header_size);
			continue;
		}

		while (header_count <= header_size && (header_count<<2) < sizeof(buffer)) {
			if (hpsb_read(host, nodeid, base + (header_count<<2), &quad, 4)) {
				HPSB_ERR("ConfigROM quadlet transaction error for %d",
					 nodeid & NODE_MASK);
				goto retry_configrom;
			}
			buffer[header_count++] = be32_to_cpu(quad);
		}
set_options:
		guid = be64_to_cpu(((u64)buffer[3] << 32) | buffer[4]);
		switch (register_guid(host, nodeid, guid, buffer[2])) {
			case -1:
				HPSB_ERR("Failed to register node in ConfigROM probe");
				continue;
			case 1:
				/* Need to probe the rest of the ConfigROM
				 * here.  */
				break;
			default:
				/* Nothing to do, this is an old unchanged
				 * node.  */
				break;
		}
        }

	/* Now check to see if we have any nodes that aren't referenced
	 * any longer.  */
        write_lock_irqsave(&node_lock, flags);
	lh = node_list.next;
	while (lh != &node_list) {
		ne = list_entry(lh, struct node_entry, list);

		/* Only checking this host */
		if (ne->host != host)
			continue;

		/* If the generation didn't get updated, then either the
		 * node was removed, or it failed the above probe. Either
		 * way, we remove references to it, since they are
		 * invalid.  */
		if (atomic_read(&ne->generation) != get_hpsb_generation())
			nodemgr_remove_node(ne);

		lh = lh->next;
	}
	write_unlock_irqrestore(&node_lock, flags);

	return;
}


struct node_entry *hpsb_guid_get_handle(u64 guid)
{
        unsigned long flags;
        struct node_entry *ne;

        read_lock_irqsave(&node_lock, flags);
        ne = find_entry(guid);
        if (ne) atomic_inc(&ne->refcount);
        read_unlock_irqrestore(&node_lock, flags);

        return ne;
}

struct hpsb_host *hpsb_get_host_by_ne(struct node_entry *ne)
{
        if (atomic_read(&ne->generation) != get_hpsb_generation()) return NULL;
        if (ne->node_id == ne->host->node_id) return ne->host;
        return NULL;
}

int hpsb_guid_fill_packet(struct node_entry *ne, struct hpsb_packet *pkt)
{
        if (atomic_read(&ne->generation) != get_hpsb_generation()) return 0;

        pkt->host = ne->host;
        pkt->node_id = ne->node_id;
        pkt->generation = atomic_read(&ne->generation);
        return 1;
}

static int nodemgr_reset_handler(void *__hi)
{
	struct host_info *hi = (struct host_info *)__hi;
	struct hpsb_host *host = hi->host;


	lock_kernel();

	siginitsetinv(&current->blocked, sigmask(SIGKILL)|sigmask(SIGINT)|sigmask(SIGTERM));

	strcpy(current->comm, "NodeMngr");

	unlock_kernel();

	do {
		current->state = TASK_INTERRUPTIBLE;
		schedule_timeout(HZ/50);

		if (hi && host) {
			nodemgr_node_probe(host);
			interruptible_sleep_on(&hi->reset_wait);
		} else
			break;

	} while (!signal_pending(current) && hi);

	return(0);
}

static void nodemgr_schedule_thread (void *__hi)
{
	struct host_info *hi = (struct host_info *)__hi;

	hi->ppid = current->pid;
	hi->pid = kernel_thread(nodemgr_reset_handler, hi, 0);
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
	hi->pid = -1;
	hi->ppid = -1;
	init_waitqueue_head(&hi->reset_wait);
	INIT_TQUEUE(&hi->task, nodemgr_schedule_thread, hi);

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
	lh = host_info_list.next;
	while (lh != &host_info_list) {
		struct host_info *myhi = list_entry(lh, struct host_info, list);
		if (myhi->host == host) {
			hi = myhi;
			break;
		}
		lh = lh->next;
	}

	if (hi == NULL) {
		HPSB_ERR ("Could not process reset of non-existent host in Node Manager");
		goto done_reset_host;
	}

	if (hi->pid >= 0) {
		wake_up(&hi->reset_wait);
	} else {
		schedule_task(&hi->task);
	}

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

	/* First remove all node entries for this host */
	write_lock_irqsave(&node_lock, flags);
	lh = node_list.next;
	while (lh != &node_list) {
		ne = list_entry(lh, struct node_entry, list);

		/* Only checking this host */
		if (ne->host != host)
			continue;

		nodemgr_remove_node(ne);
		lh = lh->next;
	}
	write_unlock_irqrestore(&node_lock, flags);

	spin_lock_irqsave (&host_info_lock, flags);
	lh = host_info_list.next;
	while (lh != &host_info_list) {
		struct host_info *myhi = list_entry(lh, struct host_info, list);
		if (myhi->host == host) {
			hi = myhi;
			break;
		}
		lh = lh->next;
	}

	if (hi == NULL) {
		HPSB_ERR ("Could not remove non-existent host in Node Manager");
		goto done_remove_host;
	}

	mb();

	if (hi->pid >= 0) {
		/* Kill the proc */
		kill_proc(hi->pid, SIGKILL, 1);

		/* XXX: We need a better way... */
		current->state = TASK_INTERRUPTIBLE;
		schedule_timeout(HZ/20);

		/* Now tell the parent to sluff off the zombied body */
		mb();
		kill_proc(hi->ppid, SIGCHLD, 1);
	}

	kfree (hi);

done_remove_host:
	spin_unlock_irqrestore (&host_info_lock, flags);

	return;
}

static struct hpsb_highlevel_ops guid_ops = {
	add_host:	nodemgr_add_host,
	host_reset:	nodemgr_host_reset,
	remove_host:	nodemgr_remove_host,
};

static struct hpsb_highlevel *hl;

void init_ieee1394_nodemgr(void)
{
        hl = hpsb_register_highlevel("Node manager", &guid_ops);
        if (!hl) {
                HPSB_ERR("Out of memory during ieee1394 initialization");
        }
}

void cleanup_ieee1394_nodemgr(void)
{
        hpsb_unregister_highlevel(hl);
}
