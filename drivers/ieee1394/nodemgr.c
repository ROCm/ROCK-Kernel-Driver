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


static atomic_t outstanding_requests;

static LIST_HEAD(node_list);
rwlock_t node_lock = RW_LOCK_UNLOCKED;

static LIST_HEAD(host_info_list);
spinlock_t host_info_lock = SPIN_LOCK_UNLOCKED;

struct host_info {
	struct hpsb_host *host;
	int pid;
	wait_queue_head_t reset_wait;
	struct list_head list;
};

struct node_entry {
        struct list_head list;
        atomic_t refcount;

        u64 guid;

        struct hpsb_host *host;
        nodeid_t node_id;
        
        atomic_t generation;
};

static struct node_entry *create_node_entry(void)
{
        struct node_entry *ge;
        unsigned long flags;

        ge = kmalloc(sizeof(struct node_entry), SLAB_ATOMIC);
        if (!ge) return NULL;

        INIT_LIST_HEAD(&ge->list);
        atomic_set(&ge->refcount, 0);
        ge->guid = (u64) -1;
        ge->host = NULL;
        ge->node_id = 0;
        atomic_set(&ge->generation, -1);

        write_lock_irqsave(&node_lock, flags);
        list_add_tail(&ge->list, &node_list);
        write_unlock_irqrestore(&node_lock, flags);

        return ge;
}

static struct node_entry *find_entry(u64 guid)
{
        struct list_head *lh;
        struct node_entry *ge;
        
        lh = node_list.next;
        while (lh != &node_list) {
                ge = list_entry(lh, struct node_entry, list);
                if (ge->guid == guid) return ge;
                lh = lh->next;
        }

        return NULL;
}

static void associate_guid(struct hpsb_host *host, nodeid_t nodeid, u64 guid)
{
        struct node_entry *ge;
        unsigned long flags;

        HPSB_DEBUG("Node %d on %s host: GUID %02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x",
                   nodeid & NODE_MASK, host->template->name, ((u8 *)&guid)[0],
		   ((u8 *)&guid)[1], ((u8 *)&guid)[2], ((u8 *)&guid)[3],
		   ((u8 *)&guid)[4], ((u8 *)&guid)[5], ((u8 *)&guid)[6],
		   ((u8 *)&guid)[7]);

        read_lock_irqsave(&node_lock, flags);
        ge = find_entry(guid);
        read_unlock_irqrestore(&node_lock, flags);

        if (!ge) ge = create_node_entry();
        if (!ge) return;

        ge->host = host;
        ge->node_id = nodeid;
        ge->guid = guid;

        atomic_set(&ge->generation, get_hpsb_generation());
}

/* This is where we probe the nodes for their information and provided
 * features.  */
static void nodemgr_node_probe(struct hpsb_host *host)
{
        struct selfid *sid = (struct selfid *)host->topology_map;
        int nodecount = host->node_count;
        nodeid_t nodeid = LOCAL_BUS;
	quadlet_t buffer[5], quad;
	octlet_t base = CSR_REGISTER_BASE + CSR_CONFIG_ROM;
	int retval;

	/* We need to detect when the ConfigROM's generation has changed,
	 * so we only update the node's info when it needs to be.  */
        for (; nodecount; nodecount--, nodeid++, sid++) {
		int header_count = 0;
		unsigned header_size = 0;
                while (sid->extended)
			sid++;
                if (!sid->link_active)
			continue;
		if (nodeid == host->node_id)
			continue;

		HPSB_DEBUG("Initiating ConfigROM request for node %d", nodeid & NODE_MASK);

		retval = hpsb_read(host, nodeid, base, &quad, 4);
		buffer[header_count++] = be32_to_cpu(quad);

		if (retval) {
			HPSB_ERR("ConfigROM quadlet transaction error for %d",
				 nodeid & NODE_MASK);
			continue;
		}

		header_size = buffer[0] >> 24;

		if (header_size < 4) {
			HPSB_INFO("Node %d on %s host has non-standard ROM format (%d quads), "
				  "cannot parse", nodeid & NODE_MASK, host->template->name,
				  header_size);
			continue;
		}

		while (header_count <= header_size && (header_count<<2) < sizeof(buffer)) {
			retval = hpsb_read(host, nodeid, base + (header_count<<2), &quad, 4);
			buffer[header_count++] = be32_to_cpu(quad);

			if (retval) {
				HPSB_ERR("ConfigROM quadlet transaction error for %d",
					 nodeid & NODE_MASK);
				goto failed_read;
			}

		}

		associate_guid(host, nodeid, be64_to_cpu(((u64)buffer[3] << 32) | buffer[4]));
failed_read:
		continue;
        }

	/* Need to detect when nodes are no longer associated with
	 * anything. I believe we can do this using the generation of the
	 * entries after a reset, compared the the hosts generation.  */
}


struct node_entry *hpsb_guid_get_handle(u64 guid)
{
        unsigned long flags;
        struct node_entry *ge;

        read_lock_irqsave(&node_lock, flags);
        ge = find_entry(guid);
        if (ge) atomic_inc(&ge->refcount);
        read_unlock_irqrestore(&node_lock, flags);

        return ge;
}

struct hpsb_host *hpsb_get_host_by_ge(struct node_entry *ge)
{
        if (atomic_read(&ge->generation) != get_hpsb_generation()) return NULL;
        if (ge->node_id == ge->host->node_id) return ge->host;
        return NULL;
}

int hpsb_guid_fill_packet(struct node_entry *ge, struct hpsb_packet *pkt)
{
        if (atomic_read(&ge->generation) != get_hpsb_generation()) return 0;

        pkt->host = ge->host;
        pkt->node_id = ge->node_id;
        pkt->generation = atomic_read(&ge->generation);
        return 1;
}

static int nodemgr_reset_handler(void *__hi)
{
	struct host_info *hi = (struct host_info *)__hi;
	struct hpsb_host *host = hi->host;

	/* Standard thread setup */
	lock_kernel();
	daemonize();
	strcpy(current->comm, "NodeMngr");
	unlock_kernel();

	for (;;) {
		if (signal_pending(current))
			break;

		/* Let's take a short pause to make sure all the devices
		 * have time to settle.  */

		current->state = TASK_INTERRUPTIBLE;
		schedule_timeout(HZ/50);

		if (hi && host)
			nodemgr_node_probe(host);

		/* Wait for the next bus reset */
		if (hi && host)
			interruptible_sleep_on(&hi->reset_wait);
	}

	return(0);
}

static void nodemgr_add_host(struct hpsb_host *host)
{
	struct host_info *hi = kmalloc (sizeof (struct host_info), GFP_KERNEL);
	int flags;

	if (!hi) {
		HPSB_ERR ("Out of memory in Node Manager");
		return;
	}

	hi->host = host;
	INIT_LIST_HEAD(&hi->list);
	hi->pid = -1;
	init_waitqueue_head(&hi->reset_wait);

	spin_lock_irqsave (&host_info_lock, flags);
	list_add_tail (&hi->list, &host_info_list);
	spin_unlock_irqrestore (&host_info_lock, flags);

	return;
}

static void nodemgr_schedule_thread (void *__hi)
{
	struct host_info *hi = (struct host_info *)__hi;

	hi->pid = kernel_thread(nodemgr_reset_handler, hi,
				CLONE_FS|CLONE_FILES|CLONE_SIGHAND);
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
		if (in_interrupt()) {
			static struct tq_struct task;
			memset(&task, 0, sizeof(struct tq_struct));

			task.routine = nodemgr_schedule_thread;
			task.data = (void*)hi;

			if (schedule_task(&task) < 0)
				HPSB_ERR ("Failed to schedule Node Manager thread!\n");
		} else {
			nodemgr_schedule_thread(hi);
		}
	}

done_reset_host:
	spin_unlock_irqrestore (&host_info_lock, flags);

	return;
}

static void nodemgr_remove_host(struct hpsb_host *host)
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
		HPSB_ERR ("Could not remove non-exitent host in Node Manager");
		goto done_remove_host;
	}

	if (hi->pid >= 0)
		kill_proc(hi->pid, SIGINT, 1);

	current->state = TASK_INTERRUPTIBLE;
	schedule_timeout(HZ*2);   /* 2 second delay */

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
        atomic_set(&outstanding_requests, 0);

        hl = hpsb_register_highlevel("Node manager", &guid_ops);
        if (!hl) {
                HPSB_ERR("Out of memory during ieee1394 initialization");
        }
}

void cleanup_ieee1394_nodemgr(void)
{
        hpsb_unregister_highlevel(hl);
}
