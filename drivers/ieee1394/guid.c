/*
 * IEEE 1394 for Linux
 *
 * GUID collection and management
 *
 * Copyright (C) 2000 Andreas E. Bombe
 *
 * This code is licensed under the GPL.  See the file COPYING in the root
 * directory of the kernel sources for details.
 */

#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/slab.h>
#include <asm/byteorder.h>
#include <asm/atomic.h>

#include "ieee1394_types.h"
#include "ieee1394.h"
#include "hosts.h"
#include "ieee1394_transactions.h"
#include "highlevel.h"
#include "csr.h"


static atomic_t outstanding_requests;

static LIST_HEAD(guid_list);
rwlock_t guid_lock = RW_LOCK_UNLOCKED;

struct guid_entry {
        struct list_head list;
        atomic_t refcount;

        u64 guid;

        struct hpsb_host *host;
        nodeid_t node_id;
        
        atomic_t generation;
};

struct guid_req {
	struct hpsb_packet *pkt;
	int retry;
	unsigned int hdr_size;
	int hdr_ptr;
	u32 bus_info[5];
	struct tq_struct tq;
};

static struct guid_entry *create_guid_entry(void)
{
        struct guid_entry *ge;
        unsigned long flags;

        ge = kmalloc(sizeof(struct guid_entry), SLAB_ATOMIC);
        if (!ge) return NULL;

        INIT_LIST_HEAD(&ge->list);
        atomic_set(&ge->refcount, 0);
        ge->guid = (u64) -1;
        ge->host = NULL;
        ge->node_id = 0;
        atomic_set(&ge->generation, -1);

        write_lock_irqsave(&guid_lock, flags);
        list_add_tail(&ge->list, &guid_list);
        write_unlock_irqrestore(&guid_lock, flags);

        return ge;
}

static struct guid_entry *find_entry(u64 guid)
{
        struct list_head *lh;
        struct guid_entry *ge;
        
        lh = guid_list.next;
        while (lh != &guid_list) {
                ge = list_entry(lh, struct guid_entry, list);
                if (ge->guid == guid) return ge;
                lh = lh->next;
        }

        return NULL;
}

static void associate_guid(struct hpsb_host *host, nodeid_t nodeid, u64 guid)
{
        struct guid_entry *ge;
        unsigned long flags;

        HPSB_DEBUG("Node %d on %s host: GUID %02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x",
                   nodeid & NODE_MASK, host->template->name, ((u8 *)&guid)[0],
		   ((u8 *)&guid)[1], ((u8 *)&guid)[2], ((u8 *)&guid)[3],
		   ((u8 *)&guid)[4], ((u8 *)&guid)[5], ((u8 *)&guid)[6],
		   ((u8 *)&guid)[7]);

        read_lock_irqsave(&guid_lock, flags);
        ge = find_entry(guid);
        read_unlock_irqrestore(&guid_lock, flags);

        if (!ge) ge = create_guid_entry();
        if (!ge) return;

        ge->host = host;
        ge->node_id = nodeid;
        ge->guid = guid;

        atomic_set(&ge->generation, get_hpsb_generation());
}

static void pkt_complete(struct guid_req *req)
{
        struct hpsb_packet *pkt = req->pkt;
	struct hpsb_host *host = pkt->host;
	nodeid_t nodeid = pkt->node_id;

	if (hpsb_packet_success (pkt)) {
		HPSB_ERR("GUID quadlet transaction error for %d, retry: %d", nodeid,
			 req->retry);
		req->retry++;
		if (req->retry > 3)
			goto finish;
		else
			goto retry;
        }

	/* Copy our received quadlet */
	req->bus_info[req->hdr_ptr++] = be32_to_cpu(pkt->header[3]);

	/* First quadlet, let's get some info */
	if (req->hdr_ptr == 1) {
		/* Get the bus_info_length from first quadlet */
		req->hdr_size = req->bus_info[0] >> 24;

		/* Make sure this isn't one of those minimal proprietary
		 * ROMs. IMO, we should just barf all over them. We need
		 * atleast four bus_info quads to get our EUI-64.  */
		if (req->hdr_size < 4) {
			HPSB_INFO("Node %d on %s host has non-standard ROM format (%d quads), "
				  "cannot parse", nodeid, host->template->name, req->hdr_size);
			goto finish;
		}

		/* Make sure we don't overflow. We have one quad for this
		 * first bus info block, the other 4 should be part of the
		 * bus info itself.  */
		if (req->hdr_size > (sizeof (req->bus_info) >> 2) - 1)
			req->hdr_size = (sizeof (req->bus_info) >> 2) - 1;
	}

	/* We've got all the info we need, so let's check the EUI-64, and
	 * add it to our list.  */
	if (req->hdr_ptr >= req->hdr_size + 1) {
		associate_guid(pkt->host, pkt->node_id,
			((u64)req->bus_info[3] << 32) | req->bus_info[4]);
		goto finish;
	}

retry:

	/* Here, we either retry a failed retrieve, or we have incremented
	 * our counter, to get the next quad in our header.  */
	free_tlabel(pkt->host, pkt->node_id, pkt->tlabel);
	free_hpsb_packet(pkt);
	pkt = hpsb_make_readqpacket(host, nodeid, CSR_REGISTER_BASE +
				    CSR_CONFIG_ROM + (req->hdr_ptr<<2));
	if (!pkt) {
		kfree(req);
		HPSB_ERR("Out of memory in GUID processing");
		return;
	}

	req->pkt = pkt;
	req->retry = 0;

	queue_task(&req->tq, &pkt->complete_tq);
	if (!hpsb_send_packet(pkt)) {
		HPSB_NOTICE("Failed to send GUID request to node %d", nodeid);
		goto finish;
	}

	return;

finish:

	free_tlabel(pkt->host, nodeid, pkt->tlabel);
	free_hpsb_packet(pkt);
	kfree(req);

	if (atomic_dec_and_test(&outstanding_requests)) {
		/* Do something useful */
	}

	return;
}


static void host_reset(struct hpsb_host *host)
{
        struct guid_req *greq;
        struct hpsb_packet *pkt;
        struct selfid *sid = (struct selfid *)host->topology_map;
        int nodecount = host->node_count;
        nodeid_t nodeid = LOCAL_BUS;

        for (; nodecount; nodecount--, nodeid++, sid++) {
                while (sid->extended)
			sid++;
                if (!sid->link_active)
			continue;
		if (nodeid == host->node_id)
			continue;

                greq = kmalloc(sizeof(struct guid_req), SLAB_ATOMIC);
                if (!greq) {
                        HPSB_ERR("Out of memory in GUID processing");
                        return;
                }

		pkt = hpsb_make_readqpacket(host, nodeid, CSR_REGISTER_BASE +
					    CSR_CONFIG_ROM);

                if (!pkt) {
                        kfree(greq);
                        HPSB_ERR("Out of memory in GUID processing");
                        return;
                }

		INIT_TQUEUE(&greq->tq, (void (*)(void*))pkt_complete, greq);

		greq->hdr_size = 4;
		greq->hdr_ptr = 0;
		greq->retry = 0;
                greq->pkt = pkt;

                queue_task(&greq->tq, &pkt->complete_tq);

                if (!hpsb_send_packet(pkt)) {
                        free_tlabel(pkt->host, pkt->node_id, pkt->tlabel);
                        free_hpsb_packet(pkt);
                        kfree(greq);
                        HPSB_NOTICE("Failed to send GUID request to node %d", nodeid);
                }

		HPSB_DEBUG("GUID request sent to node %d", nodeid & NODE_MASK);

                atomic_inc(&outstanding_requests);
        }
}


struct guid_entry *hpsb_guid_get_handle(u64 guid)
{
        unsigned long flags;
        struct guid_entry *ge;

        read_lock_irqsave(&guid_lock, flags);
        ge = find_entry(guid);
        if (ge) atomic_inc(&ge->refcount);
        read_unlock_irqrestore(&guid_lock, flags);

        return ge;
}

struct hpsb_host *hpsb_guid_localhost(struct guid_entry *ge)
{
        if (atomic_read(&ge->generation) != get_hpsb_generation()) return NULL;
        if (ge->node_id == ge->host->node_id) return ge->host;
        return NULL;
}

int hpsb_guid_fill_packet(struct guid_entry *ge, struct hpsb_packet *pkt)
{
        if (atomic_read(&ge->generation) != get_hpsb_generation()) return 0;

        pkt->host = ge->host;
        pkt->node_id = ge->node_id;
        pkt->generation = atomic_read(&ge->generation);
        return 1;
}


static struct hpsb_highlevel_ops guid_ops = {
        host_reset:  host_reset,
};

static struct hpsb_highlevel *hl;

void init_ieee1394_guid(void)
{
        atomic_set(&outstanding_requests, 0);

        hl = hpsb_register_highlevel("GUID manager", &guid_ops);
        if (!hl) {
                HPSB_ERR("out of memory during ieee1394 initialization");
        }
}

void cleanup_ieee1394_guid(void)
{
        hpsb_unregister_highlevel(hl);
}
