/*
 * IEEE 1394 for Linux
 *
 * Core support: hpsb_packet management, packet handling and forwarding to
 *               highlevel or lowlevel code
 *
 * Copyright (C) 1999, 2000 Andreas E. Bombe
 *
 * This code is licensed under the GPL.  See the file COPYING in the root
 * directory of the kernel sources for details.
 */

#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/string.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/proc_fs.h>
#include <linux/tqueue.h>
#include <asm/bitops.h>
#include <asm/byteorder.h>
#include <asm/semaphore.h>

#include "ieee1394_types.h"
#include "ieee1394.h"
#include "hosts.h"
#include "ieee1394_core.h"
#include "highlevel.h"
#include "ieee1394_transactions.h"
#include "csr.h"
#include "nodemgr.h"
#include "ieee1394_hotplug.h"

/*
 * Disable the nodemgr detection and config rom reading functionality.
 */
MODULE_PARM(disable_nodemgr, "i");
MODULE_PARM_DESC(disable_nodemgr, "Disable nodemgr functionality.");
static int disable_nodemgr = 0;

MODULE_PARM(disable_hotplug, "i");
MODULE_PARM_DESC(disable_hotplug, "Disable hotplug for detected nodes.");
static int disable_hotplug = 0;

/* We are GPL, so treat us special */
MODULE_LICENSE("GPL");

static kmem_cache_t *hpsb_packet_cache;

/* Some globals used */
const char *hpsb_speedto_str[] = { "S100", "S200", "S400" };

static void dump_packet(const char *text, quadlet_t *data, int size)
{
        int i;

        size /= 4;
        size = (size > 4 ? 4 : size);

        printk(KERN_DEBUG "ieee1394: %s", text);
        for (i = 0; i < size; i++) {
                printk(" %8.8x", data[i]);
        }
        printk("\n");
}

static void process_complete_tasks(struct hpsb_packet *packet)
{
	struct list_head *lh, *next;

	list_for_each_safe(lh, next, &packet->complete_tq) {
		struct tq_struct *tq = list_entry(lh, struct tq_struct, list);
		list_del(&tq->list);
		schedule_task(tq);
	}

	return;
}

/**
 * hpsb_add_packet_complete_task - add a new task for when a packet completes
 * @packet: the packet whose completion we want the task added to
 * @tq: the tq_struct describing the task to add
 */
void hpsb_add_packet_complete_task(struct hpsb_packet *packet, struct tq_struct *tq)
{
	list_add_tail(&tq->list, &packet->complete_tq);
	return;
}

/**
 * alloc_hpsb_packet - allocate new packet structure
 * @data_size: size of the data block to be allocated
 *
 * This function allocates, initializes and returns a new &struct hpsb_packet.
 * It can be used in interrupt context.  A header block is always included, its
 * size is big enough to contain all possible 1394 headers.  The data block is
 * only allocated when @data_size is not zero.
 *
 * For packets for which responses will be received the @data_size has to be big
 * enough to contain the response's data block since no further allocation
 * occurs at response matching time.
 *
 * The packet's generation value will be set to the current generation number
 * for ease of use.  Remember to overwrite it with your own recorded generation
 * number if you can not be sure that your code will not race with a bus reset.
 *
 * Return value: A pointer to a &struct hpsb_packet or NULL on allocation
 * failure.
 */
struct hpsb_packet *alloc_hpsb_packet(size_t data_size)
{
        struct hpsb_packet *packet = NULL;
        void *data = NULL;
        int kmflags = in_interrupt() ? GFP_ATOMIC : GFP_KERNEL;

        packet = kmem_cache_alloc(hpsb_packet_cache, kmflags);
        if (packet == NULL)
                return NULL;

        memset(packet, 0, sizeof(struct hpsb_packet));
        packet->header = packet->embedded_header;

        if (data_size) {
                data = kmalloc(data_size + 8, kmflags);
                if (data == NULL) {
			kmem_cache_free(hpsb_packet_cache, packet);
                        return NULL;
                }

                packet->data = data;
                packet->data_size = data_size;
        }

        INIT_LIST_HEAD(&packet->complete_tq);
        INIT_LIST_HEAD(&packet->list);
        sema_init(&packet->state_change, 0);
        packet->state = hpsb_unused;
        packet->generation = -1;
        packet->data_be = 1;

        return packet;
}


/**
 * free_hpsb_packet - free packet and data associated with it
 * @packet: packet to free (is NULL safe)
 *
 * This function will free packet->data, packet->header and finally the packet
 * itself.
 */
void free_hpsb_packet(struct hpsb_packet *packet)
{
        if (!packet) return;

        kfree(packet->data);
        kmem_cache_free(hpsb_packet_cache, packet);
}


int hpsb_reset_bus(struct hpsb_host *host, int type)
{
        if (!host->in_bus_reset) {
                host->driver->devctl(host, RESET_BUS, type);
                return 0;
        } else {
                return 1;
        }
}


int hpsb_bus_reset(struct hpsb_host *host)
{
        if (host->in_bus_reset) {
                HPSB_NOTICE("%s called while bus reset already in progress",
			    __FUNCTION__);
                return 1;
        }

        abort_requests(host);
        host->in_bus_reset = 1;
        host->irm_id = -1;
	host->is_irm = 0;
        host->busmgr_id = -1;
	host->is_busmgr = 0;
	host->is_cycmst = 0;
        host->node_count = 0;
        host->selfid_count = 0;

        return 0;
}


/*
 * Verify num_of_selfids SelfIDs and return number of nodes.  Return zero in
 * case verification failed.
 */
static int check_selfids(struct hpsb_host *host)
{
        int nodeid = -1;
        int rest_of_selfids = host->selfid_count;
        struct selfid *sid = (struct selfid *)host->topology_map;
        struct ext_selfid *esid;
        int esid_seq = 23;

	host->nodes_active = 0;

        while (rest_of_selfids--) {
                if (!sid->extended) {
                        nodeid++;
                        esid_seq = 0;
                        
                        if (sid->phy_id != nodeid) {
                                HPSB_INFO("SelfIDs failed monotony check with "
                                          "%d", sid->phy_id);
                                return 0;
                        }
                        
			if (sid->link_active) {
				host->nodes_active++;
				if (sid->contender)
					host->irm_id = LOCAL_BUS | sid->phy_id;
			}
                } else {
                        esid = (struct ext_selfid *)sid;

                        if ((esid->phy_id != nodeid) 
                            || (esid->seq_nr != esid_seq)) {
                                HPSB_INFO("SelfIDs failed monotony check with "
                                          "%d/%d", esid->phy_id, esid->seq_nr);
                                return 0;
                        }
                        esid_seq++;
                }
                sid++;
        }
        
        esid = (struct ext_selfid *)(sid - 1);
        while (esid->extended) {
                if ((esid->porta == 0x2) || (esid->portb == 0x2)
                    || (esid->portc == 0x2) || (esid->portd == 0x2)
                    || (esid->porte == 0x2) || (esid->portf == 0x2)
                    || (esid->portg == 0x2) || (esid->porth == 0x2)) {
                                HPSB_INFO("SelfIDs failed root check on "
                                          "extended SelfID");
                                return 0;
                }
                esid--;
        }

        sid = (struct selfid *)esid;
        if ((sid->port0 == 0x2) || (sid->port1 == 0x2) || (sid->port2 == 0x2)) {
                        HPSB_INFO("SelfIDs failed root check");
                        return 0;
        }

	host->node_count = nodeid + 1;
        return 1;
}

static void build_speed_map(struct hpsb_host *host, int nodecount)
{
        char speedcap[nodecount];
        char cldcnt[nodecount];
        u8 *map = host->speed_map;
        struct selfid *sid;
        struct ext_selfid *esid;
        int i, j, n;

        for (i = 0; i < (nodecount * 64); i += 64) {
                for (j = 0; j < nodecount; j++) {
                        map[i+j] = SPEED_400;
                }
        }

        for (i = 0; i < nodecount; i++) {
                cldcnt[i] = 0;
        }

        /* find direct children count and speed */
        for (sid = (struct selfid *)&host->topology_map[host->selfid_count-1],
                     n = nodecount - 1;
             (void *)sid >= (void *)host->topology_map; sid--) {
                if (sid->extended) {
                        esid = (struct ext_selfid *)sid;

                        if (esid->porta == 0x3) cldcnt[n]++;
                        if (esid->portb == 0x3) cldcnt[n]++;
                        if (esid->portc == 0x3) cldcnt[n]++;
                        if (esid->portd == 0x3) cldcnt[n]++;
                        if (esid->porte == 0x3) cldcnt[n]++;
                        if (esid->portf == 0x3) cldcnt[n]++;
                        if (esid->portg == 0x3) cldcnt[n]++;
                        if (esid->porth == 0x3) cldcnt[n]++;
                } else {
                        if (sid->port0 == 0x3) cldcnt[n]++;
                        if (sid->port1 == 0x3) cldcnt[n]++;
                        if (sid->port2 == 0x3) cldcnt[n]++;

                        speedcap[n] = sid->speed;
                        n--;
                }
        }

        /* set self mapping */
        for (i = 0; i < nodecount; i++) {
                map[64*i + i] = speedcap[i];
        }

        /* fix up direct children count to total children count;
         * also fix up speedcaps for sibling and parent communication */
        for (i = 1; i < nodecount; i++) {
                for (j = cldcnt[i], n = i - 1; j > 0; j--) {
                        cldcnt[i] += cldcnt[n];
                        speedcap[n] = MIN(speedcap[n], speedcap[i]);
                        n -= cldcnt[n] + 1;
                }
        }

        for (n = 0; n < nodecount; n++) {
                for (i = n - cldcnt[n]; i <= n; i++) {
                        for (j = 0; j < (n - cldcnt[n]); j++) {
                                map[j*64 + i] = map[i*64 + j] =
                                        MIN(map[i*64 + j], speedcap[n]);
                        }
                        for (j = n + 1; j < nodecount; j++) {
                                map[j*64 + i] = map[i*64 + j] =
                                        MIN(map[i*64 + j], speedcap[n]);
                        }
                }
        }
}


void hpsb_selfid_received(struct hpsb_host *host, quadlet_t sid)
{
        if (host->in_bus_reset) {
#ifdef CONFIG_IEEE1394_VERBOSEDEBUG
                HPSB_INFO("Including SelfID 0x%x", sid);
#endif
                host->topology_map[host->selfid_count++] = sid;
        } else {
                HPSB_NOTICE("Spurious SelfID packet (0x%08x) received from bus %d",
			    sid, (host->node_id & BUS_MASK) >> 6);
        }
}

void hpsb_selfid_complete(struct hpsb_host *host, int phyid, int isroot)
{
	if (!host->in_bus_reset)
		HPSB_NOTICE("SelfID completion called outside of bus reset!");

        host->node_id = LOCAL_BUS | phyid;
        host->is_root = isroot;

        if (!check_selfids(host)) {
                if (host->reset_retries++ < 20) {
                        /* selfid stage did not complete without error */
                        HPSB_NOTICE("Error in SelfID stage, resetting");
			host->in_bus_reset = 0;
                        hpsb_reset_bus(host, LONG_RESET);
                        return;
                } else {
                        HPSB_NOTICE("Stopping out-of-control reset loop");
                        HPSB_NOTICE("Warning - topology map and speed map will not be valid");
                }
        } else {
                build_speed_map(host, host->node_count);
        }

        /* irm_id is kept up to date by check_selfids() */
        if (host->irm_id == host->node_id) {
                host->is_irm = 1;
                host->is_busmgr = 1;
                host->busmgr_id = host->node_id;
                host->csr.bus_manager_id = host->node_id;
        }

        host->reset_retries = 0;
        if (isroot) {
		host->driver->devctl(host, ACT_CYCLE_MASTER, 1);
		host->is_cycmst = 1;
	}
	atomic_inc(&host->generation);
	host->in_bus_reset = 0;
        highlevel_host_reset(host);
}


void hpsb_packet_sent(struct hpsb_host *host, struct hpsb_packet *packet, 
                      int ackcode)
{
        unsigned long flags;

        packet->ack_code = ackcode;

        if (packet->no_waiter) {
                /* must not have a tlabel allocated */
                free_hpsb_packet(packet);
                return;
        }

        if (ackcode != ACK_PENDING || !packet->expect_response) {
                packet->state = hpsb_complete;
                up(&packet->state_change);
                up(&packet->state_change);
                process_complete_tasks(packet);
                return;
        }

        packet->state = hpsb_pending;
        packet->sendtime = jiffies;

        spin_lock_irqsave(&host->pending_pkt_lock, flags);
        list_add_tail(&packet->list, &host->pending_packets);
        spin_unlock_irqrestore(&host->pending_pkt_lock, flags);

        up(&packet->state_change);
        schedule_task(&host->timeout_tq);
}

/**
 * hpsb_send_packet - transmit a packet on the bus
 * @packet: packet to send
 *
 * The packet is sent through the host specified in the packet->host field.
 * Before sending, the packet's transmit speed is automatically determined using
 * the local speed map when it is an async, non-broadcast packet.
 *
 * Possibilities for failure are that host is either not initialized, in bus
 * reset, the packet's generation number doesn't match the current generation
 * number or the host reports a transmit error.
 *
 * Return value: False (0) on failure, true (1) otherwise.
 */
int hpsb_send_packet(struct hpsb_packet *packet)
{
        struct hpsb_host *host = packet->host;

        if (host->is_shutdown || host->in_bus_reset
            || (packet->generation != get_hpsb_generation(host))) {
                return 0;
        }

        packet->state = hpsb_queued;

        if (packet->type == hpsb_async && packet->node_id != ALL_NODES) {
                packet->speed_code =
                        host->speed_map[(host->node_id & NODE_MASK) * 64
                                       + (packet->node_id & NODE_MASK)];
        }

#ifdef CONFIG_IEEE1394_VERBOSEDEBUG
        switch (packet->speed_code) {
        case 2:
                dump_packet("send packet 400:", packet->header,
                            packet->header_size);
                break;
        case 1:
                dump_packet("send packet 200:", packet->header,
                            packet->header_size);
                break;
        default:
                dump_packet("send packet 100:", packet->header,
                            packet->header_size);
        }
#endif

        return host->driver->transmit_packet(host, packet);
}

static void send_packet_nocare(struct hpsb_packet *packet)
{
        if (!hpsb_send_packet(packet)) {
                free_hpsb_packet(packet);
        }
}


void handle_packet_response(struct hpsb_host *host, int tcode, quadlet_t *data,
                            size_t size)
{
        struct hpsb_packet *packet = NULL;
        struct list_head *lh;
        int tcode_match = 0;
        int tlabel;
        unsigned long flags;

        tlabel = (data[0] >> 10) & 0x3f;

        spin_lock_irqsave(&host->pending_pkt_lock, flags);

        list_for_each(lh, &host->pending_packets) {
                packet = list_entry(lh, struct hpsb_packet, list);
                if ((packet->tlabel == tlabel)
                    && (packet->node_id == (data[1] >> 16))){
                        break;
                }
        }

        if (lh == &host->pending_packets) {
                HPSB_DEBUG("unsolicited response packet received - np");
                dump_packet("contents:", data, 16);
                spin_unlock_irqrestore(&host->pending_pkt_lock, flags);
                return;
        }

        switch (packet->tcode) {
        case TCODE_WRITEQ:
        case TCODE_WRITEB:
                if (tcode == TCODE_WRITE_RESPONSE) tcode_match = 1;
                break;
        case TCODE_READQ:
                if (tcode == TCODE_READQ_RESPONSE) tcode_match = 1;
                break;
        case TCODE_READB:
                if (tcode == TCODE_READB_RESPONSE) tcode_match = 1;
                break;
        case TCODE_LOCK_REQUEST:
                if (tcode == TCODE_LOCK_RESPONSE) tcode_match = 1;
                break;
        }

        if (!tcode_match || (packet->tlabel != tlabel)
            || (packet->node_id != (data[1] >> 16))) {
                HPSB_INFO("unsolicited response packet received");
                dump_packet("contents:", data, 16);

                spin_unlock_irqrestore(&host->pending_pkt_lock, flags);
                return;
        }

        list_del(&packet->list);

        spin_unlock_irqrestore(&host->pending_pkt_lock, flags);

        /* FIXME - update size fields? */
        switch (tcode) {
        case TCODE_WRITE_RESPONSE:
                memcpy(packet->header, data, 12);
                break;
        case TCODE_READQ_RESPONSE:
                memcpy(packet->header, data, 16);
                break;
        case TCODE_READB_RESPONSE:
                memcpy(packet->header, data, 16);
                memcpy(packet->data, data + 4, size - 16);
                break;
        case TCODE_LOCK_RESPONSE:
                memcpy(packet->header, data, 16);
                memcpy(packet->data, data + 4, (size - 16) > 8 ? 8 : size - 16);
                break;
        }

        packet->state = hpsb_complete;
        up(&packet->state_change);
	process_complete_tasks(packet);
}


static struct hpsb_packet *create_reply_packet(struct hpsb_host *host,
					       quadlet_t *data, size_t dsize)
{
        struct hpsb_packet *p;

        dsize += (dsize % 4 ? 4 - (dsize % 4) : 0);

        p = alloc_hpsb_packet(dsize);
        if (p == NULL) {
                /* FIXME - send data_error response */
                return NULL;
        }

        p->type = hpsb_async;
        p->state = hpsb_unused;
        p->host = host;
        p->node_id = data[1] >> 16;
        p->tlabel = (data[0] >> 10) & 0x3f;
        p->no_waiter = 1;

	p->generation = get_hpsb_generation(host);

        if (dsize % 4) {
                p->data[dsize / 4] = 0;
        }

        return p;
}

#define PREP_REPLY_PACKET(length) \
                packet = create_reply_packet(host, data, length); \
                if (packet == NULL) break

static void handle_incoming_packet(struct hpsb_host *host, int tcode,
				   quadlet_t *data, size_t size, int write_acked)
{
        struct hpsb_packet *packet;
        int length, rcode, extcode;
        nodeid_t source = data[1] >> 16;
	nodeid_t dest = data[0] >> 16;
        u64 addr;

        /* big FIXME - no error checking is done for an out of bounds length */

        switch (tcode) {
        case TCODE_WRITEQ:
                addr = (((u64)(data[1] & 0xffff)) << 32) | data[2];
                rcode = highlevel_write(host, source, dest, data+3,
					addr, 4);

                if (!write_acked
                    && ((data[0] >> 16) & NODE_MASK) != NODE_MASK) {
                        /* not a broadcast write, reply */
                        PREP_REPLY_PACKET(0);
                        fill_async_write_resp(packet, rcode);
                        send_packet_nocare(packet);
                }
                break;

        case TCODE_WRITEB:
                addr = (((u64)(data[1] & 0xffff)) << 32) | data[2];
                rcode = highlevel_write(host, source, dest, data+4,
					addr, data[3]>>16);

                if (!write_acked
                    && ((data[0] >> 16) & NODE_MASK) != NODE_MASK) {
                        /* not a broadcast write, reply */
                        PREP_REPLY_PACKET(0);
                        fill_async_write_resp(packet, rcode);
                        send_packet_nocare(packet);
                }
                break;

        case TCODE_READQ:
                PREP_REPLY_PACKET(0);

                addr = (((u64)(data[1] & 0xffff)) << 32) | data[2];
                rcode = highlevel_read(host, source, data, addr, 4);
                fill_async_readquad_resp(packet, rcode, *data);
                send_packet_nocare(packet);
                break;

        case TCODE_READB:
                length = data[3] >> 16;
                PREP_REPLY_PACKET(length);

                addr = (((u64)(data[1] & 0xffff)) << 32) | data[2];
                rcode = highlevel_read(host, source, packet->data, addr,
                                       length);
                fill_async_readblock_resp(packet, rcode, length);
                send_packet_nocare(packet);
                break;

        case TCODE_LOCK_REQUEST:
                length = data[3] >> 16;
                extcode = data[3] & 0xffff;
                addr = (((u64)(data[1] & 0xffff)) << 32) | data[2];

                PREP_REPLY_PACKET(8);

                if ((extcode == 0) || (extcode >= 7)) {
                        /* let switch default handle error */
                        length = 0;
                }

                switch (length) {
                case 4:
                        rcode = highlevel_lock(host, source, packet->data, addr,
                                               data[4], 0, extcode);
                        fill_async_lock_resp(packet, rcode, extcode, 4);
                        break;
                case 8:
                        if ((extcode != EXTCODE_FETCH_ADD) 
                            && (extcode != EXTCODE_LITTLE_ADD)) {
                                rcode = highlevel_lock(host, source,
                                                       packet->data, addr,
                                                       data[5], data[4], 
                                                       extcode);
                                fill_async_lock_resp(packet, rcode, extcode, 4);
                        } else {
                                rcode = highlevel_lock64(host, source,
                                             (octlet_t *)packet->data, addr,
                                             *(octlet_t *)(data + 4), 0ULL,
                                             extcode);
                                fill_async_lock_resp(packet, rcode, extcode, 8);
                        }
                        break;
                case 16:
                        rcode = highlevel_lock64(host, source,
                                                 (octlet_t *)packet->data, addr,
                                                 *(octlet_t *)(data + 6),
                                                 *(octlet_t *)(data + 4), 
                                                 extcode);
                        fill_async_lock_resp(packet, rcode, extcode, 8);
                        break;
                default:
                        fill_async_lock_resp(packet, RCODE_TYPE_ERROR,
                                             extcode, 0);
                }

                send_packet_nocare(packet);
                break;
        }

}
#undef PREP_REPLY_PACKET


void hpsb_packet_received(struct hpsb_host *host, quadlet_t *data, size_t size,
                          int write_acked)
{
        int tcode;

        if (host->in_bus_reset) {
                HPSB_INFO("received packet during reset; ignoring");
                return;
        }

#ifdef CONFIG_IEEE1394_VERBOSEDEBUG
        dump_packet("received packet:", data, size);
#endif

        tcode = (data[0] >> 4) & 0xf;

        switch (tcode) {
        case TCODE_WRITE_RESPONSE:
        case TCODE_READQ_RESPONSE:
        case TCODE_READB_RESPONSE:
        case TCODE_LOCK_RESPONSE:
                handle_packet_response(host, tcode, data, size);
                break;

        case TCODE_WRITEQ:
        case TCODE_WRITEB:
        case TCODE_READQ:
        case TCODE_READB:
        case TCODE_LOCK_REQUEST:
                handle_incoming_packet(host, tcode, data, size, write_acked);
                break;


        case TCODE_ISO_DATA:
                highlevel_iso_receive(host, data, size);
                break;

        case TCODE_CYCLE_START:
                /* simply ignore this packet if it is passed on */
                break;

        default:
                HPSB_NOTICE("received packet with bogus transaction code %d", 
                            tcode);
                break;
        }
}


void abort_requests(struct hpsb_host *host)
{
        unsigned long flags;
        struct hpsb_packet *packet;
        struct list_head *lh;
        LIST_HEAD(llist);

        host->driver->devctl(host, CANCEL_REQUESTS, 0);

        spin_lock_irqsave(&host->pending_pkt_lock, flags);
        list_splice(&host->pending_packets, &llist);
        INIT_LIST_HEAD(&host->pending_packets);
        spin_unlock_irqrestore(&host->pending_pkt_lock, flags);

        list_for_each(lh, &llist) {
                packet = list_entry(lh, struct hpsb_packet, list);
                packet->state = hpsb_complete;
                packet->ack_code = ACKX_ABORTED;
                up(&packet->state_change);
		process_complete_tasks(packet);
        }
}

void abort_timedouts(struct hpsb_host *host)
{
        unsigned long flags;
        struct hpsb_packet *packet;
        unsigned long expire;
        struct list_head *lh, *next;
        LIST_HEAD(expiredlist);

        spin_lock_irqsave(&host->csr.lock, flags);
        expire = (host->csr.split_timeout_hi * 8000 
                  + (host->csr.split_timeout_lo >> 19))
                * HZ / 8000;
        /* Avoid shortening of timeout due to rounding errors: */
        expire++;
        spin_unlock_irqrestore(&host->csr.lock, flags);


        spin_lock_irqsave(&host->pending_pkt_lock, flags);

	for (lh = host->pending_packets.next; lh != &host->pending_packets; lh = next) {
                packet = list_entry(lh, struct hpsb_packet, list);
		next = lh->next;
                if (time_before(packet->sendtime + expire, jiffies)) {
                        list_del(&packet->list);
                        list_add(&packet->list, &expiredlist);
                }
        }

        if (!list_empty(&host->pending_packets))
		schedule_task(&host->timeout_tq);

        spin_unlock_irqrestore(&host->pending_pkt_lock, flags);

        list_for_each(lh, &expiredlist) {
                packet = list_entry(lh, struct hpsb_packet, list);
                packet->state = hpsb_complete;
                packet->ack_code = ACKX_TIMEOUT;
                up(&packet->state_change);
		process_complete_tasks(packet);
        }
}


/*
 * character device dispatching (see ieee1394_core.h)
 * Dan Maas <dmaas@dcine.com>
 */

static struct {
	struct file_operations *file_ops;
	struct module *module;
} ieee1394_chardevs[16];

static rwlock_t ieee1394_chardevs_lock = RW_LOCK_UNLOCKED;

static int ieee1394_dispatch_open(struct inode *inode, struct file *file);

static struct file_operations ieee1394_chardev_ops = {
	.owner =THIS_MODULE,
	.open =	ieee1394_dispatch_open,
};

devfs_handle_t ieee1394_devfs_handle;


/* claim a block of minor numbers */
int ieee1394_register_chardev(int blocknum,
			      struct module *module,
			      struct file_operations *file_ops)
{
	int retval;
	
	if( (blocknum < 0) || (blocknum > 15) )
		return -EINVAL;

	write_lock(&ieee1394_chardevs_lock);

	if(ieee1394_chardevs[blocknum].file_ops == NULL) {
		/* grab the minor block */
		ieee1394_chardevs[blocknum].file_ops = file_ops;
		ieee1394_chardevs[blocknum].module = module;
		
		retval = 0;
	} else {
		/* block already taken */
		retval = -EBUSY;
	}
	
	write_unlock(&ieee1394_chardevs_lock);

	return retval;
}

/* release a block of minor numbers */
void ieee1394_unregister_chardev(int blocknum)
{
	if( (blocknum < 0) || (blocknum > 15) )
		return;
	
	write_lock(&ieee1394_chardevs_lock);
	
	if(ieee1394_chardevs[blocknum].file_ops) {
		ieee1394_chardevs[blocknum].file_ops = NULL;
		ieee1394_chardevs[blocknum].module = NULL;
	}
	
	write_unlock(&ieee1394_chardevs_lock);
}

/*
  ieee1394_get_chardev() - look up and acquire a character device
  driver that has previously registered using ieee1394_register_chardev()
  
  On success, returns 1 and sets module and file_ops to the driver.
  The module will have an incremented reference count.
   
  On failure, returns 0.
  The module will NOT have an incremented reference count.
*/

static int ieee1394_get_chardev(int blocknum,
				struct module **module,
				struct file_operations **file_ops)
{
	int ret = 0;
       
	if( (blocknum < 0) || (blocknum > 15) )
		return ret;

	read_lock(&ieee1394_chardevs_lock);

	*module = ieee1394_chardevs[blocknum].module;
	*file_ops = ieee1394_chardevs[blocknum].file_ops;

	if(*file_ops == NULL)
		goto out;

	/* don't need try_inc_mod_count if the driver is non-modular */
	if(*module && (try_inc_mod_count(*module) == 0))
		goto out;

	/* success! */
	ret = 1;
	
out:
	read_unlock(&ieee1394_chardevs_lock);
	return ret;
}

/* the point of entry for open() on any ieee1394 character device */
static int ieee1394_dispatch_open(struct inode *inode, struct file *file)
{
	struct file_operations *file_ops;
	struct module *module;
	int blocknum;
	int retval;

	/*
	  Maintaining correct module reference counts is tricky here!

	  The key thing to remember is that the VFS increments the
	  reference count of ieee1394 before it calls
	  ieee1394_dispatch_open().

	  If the open() succeeds, then we need to transfer this extra
	  reference to the task-specific driver module (e.g. raw1394).
	  The VFS will deref the driver module automatically when the
	  file is later released.

	  If the open() fails, then the VFS will drop the
	  reference count of whatever module file->f_op->owner points
	  to, immediately after this function returns.
	*/
	
        /* shift away lower four bits of the minor
	   to get the index of the ieee1394_driver
	   we want */
	
	blocknum = (minor(inode->i_rdev) >> 4) & 0xF;

	/* look up the driver */

	if(ieee1394_get_chardev(blocknum, &module, &file_ops) == 0)
		return -ENODEV;

	/* redirect all subsequent requests to the driver's
	   own file_operations */
	file->f_op = file_ops;

	/* at this point BOTH ieee1394 and the task-specific driver have
	   an extra reference */

	/* follow through with the open() */
	retval = file_ops->open(inode, file);

	if(retval == 0) {
		
		/* If the open() succeeded, then ieee1394 will be left
		   with an extra module reference, so we discard it here.

		   The task-specific driver still has the extra
		   reference given to it by ieee1394_get_chardev().
		   This extra reference prevents the module from
		   unloading while the file is open, and will be
		   dropped by the VFS when the file is released.
		*/
		
		if(THIS_MODULE)
			__MOD_DEC_USE_COUNT((struct module*) THIS_MODULE);
		
		/* note that if ieee1394 is compiled into the kernel,
		   THIS_MODULE will be (void*) NULL, hence the if and
		   the cast are necessary */

	} else {

		/* if the open() failed, then we need to drop the
		   extra reference we gave to the task-specific
		   driver */
		
		if(module)
			__MOD_DEC_USE_COUNT(module);
	
		/* point the file's f_ops back to ieee1394. The VFS will then
		   decrement ieee1394's reference count immediately after this
		   function returns. */
		
		file->f_op = &ieee1394_chardev_ops;
	}

	return retval;
}

struct proc_dir_entry *ieee1394_procfs_entry;

static int __init ieee1394_init(void)
{
	hpsb_packet_cache = kmem_cache_create("hpsb_packet", sizeof(struct hpsb_packet),
					      0, 0, NULL, NULL);

	ieee1394_devfs_handle = devfs_mk_dir(NULL, "ieee1394", NULL);

	if (register_chrdev(IEEE1394_MAJOR, "ieee1394", &ieee1394_chardev_ops)) {
		HPSB_ERR("unable to register character device major %d!\n", IEEE1394_MAJOR);
		devfs_unregister(ieee1394_devfs_handle);
		return -ENODEV;
	}

#ifdef CONFIG_PROC_FS
	/* Must be done before we start everything else, since the drivers
	 * may use it.  */
	ieee1394_procfs_entry = proc_mkdir( "ieee1394", proc_bus);
	if (ieee1394_procfs_entry == NULL) {
		HPSB_ERR("unable to create /proc/bus/ieee1394\n");
		unregister_chrdev(IEEE1394_MAJOR, "ieee1394");
		devfs_unregister(ieee1394_devfs_handle);
		return -ENOMEM;
	}
	ieee1394_procfs_entry->owner = THIS_MODULE;
#endif

	init_hpsb_highlevel();
	init_csr();
	if (!disable_nodemgr)
		init_ieee1394_nodemgr(disable_hotplug);
	else
		HPSB_INFO("nodemgr functionality disabled");

	return 0;
}

static void __exit ieee1394_cleanup(void)
{
	if (!disable_nodemgr)
		cleanup_ieee1394_nodemgr();

	cleanup_csr();
	kmem_cache_destroy(hpsb_packet_cache);

	unregister_chrdev(IEEE1394_MAJOR, "ieee1394");
	
	/* it's ok to pass a NULL devfs_handle to devfs_unregister */
	devfs_unregister(ieee1394_devfs_handle);
	
	remove_proc_entry("ieee1394", proc_bus);
}

module_init(ieee1394_init);
module_exit(ieee1394_cleanup);

/* Exported symbols */
EXPORT_SYMBOL(hpsb_alloc_host);
EXPORT_SYMBOL(hpsb_add_host);
EXPORT_SYMBOL(hpsb_remove_host);
EXPORT_SYMBOL(hpsb_ref_host);
EXPORT_SYMBOL(hpsb_unref_host);
EXPORT_SYMBOL(hpsb_speedto_str);
EXPORT_SYMBOL(hpsb_add_packet_complete_task);

EXPORT_SYMBOL(alloc_hpsb_packet);
EXPORT_SYMBOL(free_hpsb_packet);
EXPORT_SYMBOL(hpsb_send_packet);
EXPORT_SYMBOL(hpsb_reset_bus);
EXPORT_SYMBOL(hpsb_bus_reset);
EXPORT_SYMBOL(hpsb_selfid_received);
EXPORT_SYMBOL(hpsb_selfid_complete);
EXPORT_SYMBOL(hpsb_packet_sent);
EXPORT_SYMBOL(hpsb_packet_received);

EXPORT_SYMBOL(get_tlabel);
EXPORT_SYMBOL(free_tlabel);
EXPORT_SYMBOL(fill_async_readquad);
EXPORT_SYMBOL(fill_async_readquad_resp);
EXPORT_SYMBOL(fill_async_readblock);
EXPORT_SYMBOL(fill_async_readblock_resp);
EXPORT_SYMBOL(fill_async_writequad);
EXPORT_SYMBOL(fill_async_writeblock);
EXPORT_SYMBOL(fill_async_write_resp);
EXPORT_SYMBOL(fill_async_lock);
EXPORT_SYMBOL(fill_async_lock_resp);
EXPORT_SYMBOL(fill_iso_packet);
EXPORT_SYMBOL(fill_phy_packet);
EXPORT_SYMBOL(hpsb_make_readqpacket);
EXPORT_SYMBOL(hpsb_make_readbpacket);
EXPORT_SYMBOL(hpsb_make_writeqpacket);
EXPORT_SYMBOL(hpsb_make_writebpacket);
EXPORT_SYMBOL(hpsb_make_lockpacket);
EXPORT_SYMBOL(hpsb_make_phypacket);
EXPORT_SYMBOL(hpsb_packet_success);
EXPORT_SYMBOL(hpsb_make_packet);
EXPORT_SYMBOL(hpsb_read);
EXPORT_SYMBOL(hpsb_write);
EXPORT_SYMBOL(hpsb_lock);

EXPORT_SYMBOL(hpsb_register_highlevel);
EXPORT_SYMBOL(hpsb_unregister_highlevel);
EXPORT_SYMBOL(hpsb_register_addrspace);
EXPORT_SYMBOL(hpsb_listen_channel);
EXPORT_SYMBOL(hpsb_unlisten_channel);
EXPORT_SYMBOL(highlevel_read);
EXPORT_SYMBOL(highlevel_write);
EXPORT_SYMBOL(highlevel_lock);
EXPORT_SYMBOL(highlevel_lock64);
EXPORT_SYMBOL(highlevel_add_host);
EXPORT_SYMBOL(highlevel_remove_host);
EXPORT_SYMBOL(highlevel_host_reset);

EXPORT_SYMBOL(hpsb_guid_get_entry);
EXPORT_SYMBOL(hpsb_nodeid_get_entry);
EXPORT_SYMBOL(hpsb_node_fill_packet);
EXPORT_SYMBOL(hpsb_node_read);
EXPORT_SYMBOL(hpsb_node_write);
EXPORT_SYMBOL(hpsb_node_lock);
EXPORT_SYMBOL(hpsb_register_protocol);
EXPORT_SYMBOL(hpsb_unregister_protocol);
EXPORT_SYMBOL(hpsb_release_unit_directory);

EXPORT_SYMBOL(ieee1394_register_chardev);
EXPORT_SYMBOL(ieee1394_unregister_chardev);
EXPORT_SYMBOL(ieee1394_devfs_handle);

EXPORT_SYMBOL(ieee1394_procfs_entry);
