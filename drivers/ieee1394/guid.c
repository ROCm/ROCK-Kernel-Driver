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

        HPSB_DEBUG("node %d on host 0x%p has GUID 0x%08x%08x",
                   nodeid & NODE_MASK, host, (unsigned int)(guid >> 32),
                   (unsigned int)(guid & 0xffffffff));

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
        int rcode = (pkt->header[1] >> 12) & 0xf;

        if (pkt->ack_code == ACK_PENDING && rcode == RCODE_COMPLETE) {
                if (*(char *)pkt->data > 1) {
                        associate_guid(pkt->host, pkt->node_id,
                                       ((u64)be32_to_cpu(pkt->data[3]) << 32)
                                       | be32_to_cpu(pkt->data[4]));
                } else {
                        HPSB_DEBUG("minimal ROM on node %d",
                                   pkt->node_id & NODE_MASK);
                }
        } else {
                HPSB_DEBUG("guid transaction error: ack %d, rcode %d",
                           pkt->ack_code, rcode);
        }

        free_tlabel(pkt->host, pkt->node_id, pkt->tlabel);
        free_hpsb_packet(pkt);
        kfree(req);

        if (atomic_dec_and_test(&outstanding_requests)) {
                /* FIXME: free unreferenced and inactive GUID entries. */
        }
}


static void host_reset(struct hpsb_host *host)
{
        struct guid_req *greq;
        struct hpsb_packet *pkt;
        struct selfid *sid = (struct selfid *)host->topology_map;
        int nodecount = host->node_count;
        nodeid_t nodeid = LOCAL_BUS;

        for (; nodecount; nodecount--, nodeid++, sid++) {
                while (sid->extended) sid++;
                if (!sid->link_active) continue;
                if (nodeid == host->node_id) continue;

                greq = kmalloc(sizeof(struct guid_req), SLAB_ATOMIC);
                if (!greq) {
                        HPSB_ERR("out of memory in GUID processing");
                        return;
                }

                pkt = hpsb_make_readbpacket(host, nodeid,
                                            CSR_REGISTER_BASE + CSR_CONFIG_ROM,
                                            20);
                if (!pkt) {
                        kfree(greq);
                        HPSB_ERR("out of memory in GUID processing");
                        return;
                }

                INIT_LIST_HEAD(&greq->tq.list);
                greq->tq.sync = 0;
                greq->tq.routine = (void (*)(void*))pkt_complete;
                greq->tq.data = greq;
                greq->pkt = pkt;

                queue_task(&greq->tq, &pkt->complete_tq);

                if (!hpsb_send_packet(pkt)) {
                        free_tlabel(pkt->host, pkt->node_id, pkt->tlabel);
                        free_hpsb_packet(pkt);
                        kfree(greq);
                        HPSB_NOTICE("failed to send packet in GUID processing");
                }

                HPSB_INFO("GUID request sent to node %d", nodeid & NODE_MASK);
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

void init_ieee1394_guid(void)
{
        atomic_set(&outstanding_requests, 0);

        if (!hpsb_register_highlevel("GUID manager", &guid_ops)) {
                HPSB_ERR("out of memory during ieee1394 initialization");
        }
}
