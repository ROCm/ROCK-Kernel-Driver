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
#include "guid.h"


atomic_t hpsb_generation = ATOMIC_INIT(0);


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
        void *header = NULL, *data = NULL;
        int kmflags = in_interrupt() ? GFP_ATOMIC : GFP_KERNEL;

        packet = kmalloc(sizeof(struct hpsb_packet), kmflags);
        header = kmalloc(5 * 4, kmflags);
        if (header == NULL || packet == NULL) {
                kfree(header);
                kfree(packet);
                return NULL;
        }

        memset(packet, 0, sizeof(struct hpsb_packet));
        packet->header = header;

        if (data_size) {
                data = kmalloc(data_size + 8, kmflags);
                if (data == NULL) {
                        kfree(header);
                        kfree(packet);
                        return NULL;
                }

                packet->data = data;
                packet->data_size = data_size;
        }

        INIT_LIST_HEAD(&packet->list);
        sema_init(&packet->state_change, 0);
        packet->state = unused;
        packet->generation = get_hpsb_generation();
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
        if (packet == NULL) {
                return;
        }

        kfree(packet->data);
        kfree(packet->header);
        kfree(packet);
}


int hpsb_reset_bus(struct hpsb_host *host)
{
        if (!host->initialized) {
                return 1;
        }

        if (!hpsb_bus_reset(host)) {
                host->template->devctl(host, RESET_BUS, 0);
                return 0;
        } else {
                return 1;
        }
}


int hpsb_bus_reset(struct hpsb_host *host)
{
        if (host->in_bus_reset) {
                HPSB_NOTICE(__FUNCTION__ 
                            " called while bus reset already in progress");
                return 1;
        }

        abort_requests(host);
        host->in_bus_reset = 1;
        host->irm_id = -1;
        host->busmgr_id = -1;
        host->node_count = 0;
        host->selfid_count = 0;

        return 0;
}


/*
 * Verify num_of_selfids SelfIDs and return number of nodes.  Return zero in
 * case verification failed.
 */
static int check_selfids(struct hpsb_host *host, unsigned int num_of_selfids)
{
        int nodeid = -1;
        int rest_of_selfids = num_of_selfids;
        struct selfid *sid = (struct selfid *)host->topology_map;
        struct ext_selfid *esid;
        int esid_seq = 23;

        while (rest_of_selfids--) {
                if (!sid->extended) {
                        nodeid++;
                        esid_seq = 0;
                        
                        if (sid->phy_id != nodeid) {
                                HPSB_INFO("SelfIDs failed monotony check with "
                                          "%d", sid->phy_id);
                                return 0;
                        }
                        
                        if (sid->contender && sid->link_active) {
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

        return nodeid + 1;
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
        for (i = nodecount - 1; i; i--) {
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
                HPSB_DEBUG("including selfid 0x%x", sid);
                host->topology_map[host->selfid_count++] = sid;
        } else {
                /* FIXME - info on which host */
                HPSB_NOTICE("spurious selfid packet (0x%8.8x) received", sid);
        }
}

void hpsb_selfid_complete(struct hpsb_host *host, int phyid, int isroot)
{
        host->node_id = 0xffc0 | phyid;
        host->in_bus_reset = 0;
        host->is_root = isroot;

        host->node_count = check_selfids(host, host->selfid_count);
        if (!host->node_count) {
                if (host->reset_retries++ < 20) {
                        /* selfid stage did not complete without error */
                        HPSB_NOTICE("error in SelfID stage - resetting");
                        hpsb_reset_bus(host);
                        return;
                } else {
                        HPSB_NOTICE("stopping out-of-control reset loop");
                        HPSB_NOTICE("warning - topology map and speed map will "
                                    "therefore not be valid");
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
        inc_hpsb_generation();
        if (isroot) host->template->devctl(host, ACT_CYCLE_MASTER, 1);
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
                packet->state = complete;
                up(&packet->state_change);
                up(&packet->state_change);
                run_task_queue(&packet->complete_tq);
                return;
        }

        packet->state = pending;
        packet->sendtime = jiffies;

        spin_lock_irqsave(&host->pending_pkt_lock, flags);
        list_add_tail(&packet->list, &host->pending_packets);
        spin_unlock_irqrestore(&host->pending_pkt_lock, flags);

        up(&packet->state_change);
        queue_task(&host->timeout_tq, &tq_timer);
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

        if (!host->initialized || host->in_bus_reset 
            || (packet->generation != get_hpsb_generation())) {
                return 0;
        }

        packet->state = queued;

        if (packet->type == async && packet->node_id != ALL_NODES) {
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

        return host->template->transmit_packet(host, packet);
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

        lh = host->pending_packets.next;
        while (lh != &host->pending_packets) {
                packet = list_entry(lh, struct hpsb_packet, list);
                if ((packet->tlabel == tlabel)
                    && (packet->node_id == (data[1] >> 16))){
                        break;
                }
                lh = lh->next;
        }

        if (lh == &host->pending_packets) {
                HPSB_INFO("unsolicited response packet received - np");
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

        packet->state = complete;
        up(&packet->state_change);
        run_task_queue(&packet->complete_tq);
}


struct hpsb_packet *create_reply_packet(struct hpsb_host *host, quadlet_t *data,
                                        size_t dsize)
{
        struct hpsb_packet *p;

        dsize += (dsize % 4 ? 4 - (dsize % 4) : 0);

        p = alloc_hpsb_packet(dsize);
        if (p == NULL) {
                /* FIXME - send data_error response */
                return NULL;
        }

        p->type = async;
        p->state = unused;
        p->host = host;
        p->node_id = data[1] >> 16;
        p->tlabel = (data[0] >> 10) & 0x3f;
        p->no_waiter = 1;

        if (dsize % 4) {
                p->data[dsize / 4] = 0;
        }

        return p;
}

#define PREP_REPLY_PACKET(length) \
                packet = create_reply_packet(host, data, length); \
                if (packet == NULL) break

void handle_incoming_packet(struct hpsb_host *host, int tcode, quadlet_t *data,
                            size_t size, int write_acked)
{
        struct hpsb_packet *packet;
        int length, rcode, extcode;
        int source = data[1] >> 16;
        u64 addr;

        /* big FIXME - no error checking is done for an out of bounds length */

        switch (tcode) {
        case TCODE_WRITEQ:
                addr = (((u64)(data[1] & 0xffff)) << 32) | data[2];
                rcode = highlevel_write(host, source, data+3, addr, 4);

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
                rcode = highlevel_write(host, source, data+4, addr,
                                        data[3]>>16);

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

        host->template->devctl(host, CANCEL_REQUESTS, 0);

        spin_lock_irqsave(&host->pending_pkt_lock, flags);
        list_splice(&host->pending_packets, &llist);
        INIT_LIST_HEAD(&host->pending_packets);
        spin_unlock_irqrestore(&host->pending_pkt_lock, flags);

        lh = llist.next;

        while (lh != &llist) {
                packet = list_entry(lh, struct hpsb_packet, list);
                lh = lh->next;
                packet->state = complete;
                packet->ack_code = ACKX_ABORTED;
                up(&packet->state_change);
                run_task_queue(&packet->complete_tq);
        }
}

void abort_timedouts(struct hpsb_host *host)
{
        unsigned long flags;
        struct hpsb_packet *packet;
        unsigned long expire;
        struct list_head *lh;
        LIST_HEAD(expiredlist);

        spin_lock_irqsave(&host->csr.lock, flags);
        expire = (host->csr.split_timeout_hi * 8000 
                  + (host->csr.split_timeout_lo >> 19))
                * HZ / 8000;
        /* Avoid shortening of timeout due to rounding errors: */
        expire++;
        spin_unlock_irqrestore(&host->csr.lock, flags);


        spin_lock_irqsave(&host->pending_pkt_lock, flags);
        lh = host->pending_packets.next;

        while (lh != &host->pending_packets) {
                packet = list_entry(lh, struct hpsb_packet, list);
                lh = lh->next;
                if (time_before(packet->sendtime + expire, jiffies)) {
                        list_del(&packet->list);
                        list_add(&packet->list, &expiredlist);
                }
        }

        if (!list_empty(&host->pending_packets)) {
                queue_task(&host->timeout_tq, &tq_timer);
        }
        spin_unlock_irqrestore(&host->pending_pkt_lock, flags);

        lh = expiredlist.next;
        while (lh != &expiredlist) {
                packet = list_entry(lh, struct hpsb_packet, list);
                lh = lh->next;
                packet->state = complete;
                packet->ack_code = ACKX_TIMEOUT;
                up(&packet->state_change);
                run_task_queue(&packet->complete_tq);
        }
}


#ifndef MODULE

void __init ieee1394_init(void)
{
        register_builtin_lowlevels();
        init_hpsb_highlevel();
        init_csr();
        init_ieee1394_guid();
}

#else

int init_module(void)
{
        init_hpsb_highlevel();
        init_csr();
        init_ieee1394_guid();

        return 0;
}

#endif
