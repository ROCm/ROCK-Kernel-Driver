/*
 * IEEE 1394 for Linux
 *
 * Transaction support.
 *
 * Copyright (C) 1999 Andreas E. Bombe
 *
 * This code is licensed under the GPL.  See the file COPYING in the root
 * directory of the kernel sources for details.
 */

#include <linux/sched.h>
#include <asm/errno.h>
#include <asm/bitops.h>

#include "ieee1394.h"
#include "ieee1394_types.h"
#include "hosts.h"
#include "ieee1394_core.h"
#include "highlevel.h"


#define PREP_ASYNC_HEAD_ADDRESS(tc) \
        packet->tcode = tc; \
        packet->header[0] = (packet->node_id << 16) | (packet->tlabel << 10) \
                | (1 << 8) | (tc << 4); \
        packet->header[1] = (packet->host->node_id << 16) | (addr >> 32); \
        packet->header[2] = addr & 0xffffffff

#define PREP_ASYNC_HEAD_RCODE(tc) \
        packet->tcode = tc; \
        packet->header[0] = (packet->node_id << 16) | (packet->tlabel << 10) \
                | (1 << 8) | (tc << 4); \
        packet->header[1] = (packet->host->node_id << 16) | (rcode << 12); \
        packet->header[2] = 0


void fill_async_readquad(struct hpsb_packet *packet, u64 addr)
{
        PREP_ASYNC_HEAD_ADDRESS(TCODE_READQ);
        packet->header_size = 12;
        packet->data_size = 0;
        packet->expect_response = 1;
}

void fill_async_readquad_resp(struct hpsb_packet *packet, int rcode, 
                              quadlet_t data)
{
        PREP_ASYNC_HEAD_RCODE(TCODE_READQ_RESPONSE);
        packet->header[3] = data;
        packet->header_size = 16;
        packet->data_size = 0;
}

void fill_async_readblock(struct hpsb_packet *packet, u64 addr, int length)
{
        PREP_ASYNC_HEAD_ADDRESS(TCODE_READB);
        packet->header[3] = length << 16;
        packet->header_size = 16;
        packet->data_size = 0;
        packet->expect_response = 1;
}

void fill_async_readblock_resp(struct hpsb_packet *packet, int rcode, 
                               int length)
{
        if (rcode != RCODE_COMPLETE) {
                length = 0;
        }

        PREP_ASYNC_HEAD_RCODE(TCODE_READB_RESPONSE);
        packet->header[3] = length << 16;
        packet->header_size = 16;
        packet->data_size = length + (length % 4 ? 4 - (length % 4) : 0);
}

void fill_async_writequad(struct hpsb_packet *packet, u64 addr, quadlet_t data)
{
        PREP_ASYNC_HEAD_ADDRESS(TCODE_WRITEQ);
        packet->header[3] = data;
        packet->header_size = 16;
        packet->data_size = 0;
        packet->expect_response = 1;
}

void fill_async_writeblock(struct hpsb_packet *packet, u64 addr, int length)
{
        PREP_ASYNC_HEAD_ADDRESS(TCODE_WRITEB);
        packet->header[3] = length << 16;
        packet->header_size = 16;
        packet->expect_response = 1;
        packet->data_size = length + (length % 4 ? 4 - (length % 4) : 0);
}

void fill_async_write_resp(struct hpsb_packet *packet, int rcode)
{
        PREP_ASYNC_HEAD_RCODE(TCODE_WRITE_RESPONSE);
        packet->header[2] = 0;
        packet->header_size = 12;
        packet->data_size = 0;
}

void fill_async_lock(struct hpsb_packet *packet, u64 addr, int extcode, 
                     int length)
{
        PREP_ASYNC_HEAD_ADDRESS(TCODE_LOCK_REQUEST);
        packet->header[3] = (length << 16) | extcode;
        packet->header_size = 16;
        packet->data_size = length;
        packet->expect_response = 1;
}

void fill_async_lock_resp(struct hpsb_packet *packet, int rcode, int extcode, 
                          int length)
{
        if (rcode != RCODE_COMPLETE) {
                length = 0;
        }

        PREP_ASYNC_HEAD_RCODE(TCODE_LOCK_RESPONSE);
        packet->header[3] = (length << 16) | extcode;
        packet->header_size = 16;
        packet->data_size = length;
}

void fill_iso_packet(struct hpsb_packet *packet, int length, int channel,
                     int tag, int sync)
{
        packet->header[0] = (length << 16) | (tag << 14) | (channel << 8)
                | (TCODE_ISO_DATA << 4) | sync;

        packet->header_size = 4;
        packet->data_size = length;
        packet->tcode = TCODE_ISO_DATA;
}


/**
 * get_tlabel - allocate a transaction label
 * @host: host to be used for transmission
 * @nodeid: the node ID of the transmission target
 * @wait: whether to sleep if no tlabel is available
 *
 * Every asynchronous transaction on the 1394 bus needs a transaction label to
 * match the response to the request.  This label has to be different from any
 * other transaction label in an outstanding request to the same node to make
 * matching possible without ambiguity.
 *
 * There are 64 different tlabels, so an allocated tlabel has to be freed with
 * free_tlabel() after the transaction is complete (unless it's reused again for
 * the same target node).
 *
 * @wait must not be set to true if you are calling from interrupt context.
 *
 * Return value: The allocated transaction label or -1 if there was no free
 * tlabel and @wait is false.
 */
int get_tlabel(struct hpsb_host *host, nodeid_t nodeid, int wait)
{
	int tlabel;
	unsigned long flags;

	if (wait) {
		down(&host->tlabel_count);
	} else {
		if (down_trylock(&host->tlabel_count)) return -1;
	}

	spin_lock_irqsave(&host->tlabel_lock, flags);

	if (host->tlabel_pool[0] != ~0) {
		tlabel = ffz(host->tlabel_pool[0]);
		host->tlabel_pool[0] |= 1 << tlabel;
	} else {
		tlabel = ffz(host->tlabel_pool[1]);
		host->tlabel_pool[1] |= 1 << tlabel;
		tlabel += 32;
	}

	spin_unlock_irqrestore(&host->tlabel_lock, flags);

	return tlabel;
}

/**
 * free_tlabel - free an allocated transaction label
 * @host: host to be used for transmission
 * @nodeid: the node ID of the transmission target
 * @tlabel: the transaction label to free
 *
 * Frees the transaction label allocated with get_tlabel().  The tlabel has to
 * be freed after the transaction is complete (i.e. response was received for a
 * split transaction or packet was sent for a unified transaction).
 *
 * A tlabel must not be freed twice.
 */
void free_tlabel(struct hpsb_host *host, nodeid_t nodeid, int tlabel)
{
        unsigned long flags;

        spin_lock_irqsave(&host->tlabel_lock, flags);

        if (tlabel < 32) {
                host->tlabel_pool[0] &= ~(1 << tlabel);
        } else {
                host->tlabel_pool[1] &= ~(1 << (tlabel-32));
        }

        spin_unlock_irqrestore(&host->tlabel_lock, flags);

        up(&host->tlabel_count);
}



int hpsb_packet_success(struct hpsb_packet *packet)
{
        switch (packet->ack_code) {
        case ACK_PENDING:
                switch ((packet->header[1] >> 12) & 0xf) {
                case RCODE_COMPLETE:
                        return 0;
                case RCODE_CONFLICT_ERROR:
                        return -EAGAIN;
                case RCODE_DATA_ERROR:
                        return -EREMOTEIO;
                case RCODE_TYPE_ERROR:
                        return -EACCES;
                case RCODE_ADDRESS_ERROR:
                        return -EINVAL;
                default:
                        HPSB_ERR("received reserved rcode %d from node %d",
                                 (packet->header[1] >> 12) & 0xf, 
                                 packet->node_id);
                        return -EAGAIN;
                }
                HPSB_PANIC("reached unreachable code 1 in " __FUNCTION__);

        case ACK_BUSY_X:
        case ACK_BUSY_A:
        case ACK_BUSY_B:
                return -EBUSY;

        case ACK_TYPE_ERROR:
                return -EACCES;

        case ACK_COMPLETE:
                if (packet->tcode == TCODE_WRITEQ
                    || packet->tcode == TCODE_WRITEB) {
                        return 0;
                } else {
                        HPSB_ERR("impossible ack_complete from node %d "
                                 "(tcode %d)", packet->node_id, packet->tcode);
                        return -EAGAIN;
                }


        case ACK_DATA_ERROR:
                if (packet->tcode == TCODE_WRITEB
                    || packet->tcode == TCODE_LOCK_REQUEST) {
                        return -EAGAIN;
                } else {
                        HPSB_ERR("impossible ack_data_error from node %d "
                                 "(tcode %d)", packet->node_id, packet->tcode);
                        return -EAGAIN;
                }

        case ACKX_NONE:
        case ACKX_SEND_ERROR:
        case ACKX_ABORTED:
        case ACKX_TIMEOUT:
                /* error while sending */
                return -EAGAIN;

        default:
                HPSB_ERR("got invalid ack %d from node %d (tcode %d)",
                         packet->ack_code, packet->node_id, packet->tcode);
                return -EAGAIN;
        }

        HPSB_PANIC("reached unreachable code 2 in " __FUNCTION__);
}


int hpsb_read_trylocal(struct hpsb_host *host, nodeid_t node, u64 addr, 
                       quadlet_t *buffer, size_t length)
{
        if (host->node_id != node) return -1;
        return highlevel_read(host, node, buffer, addr, length);
}

struct hpsb_packet *hpsb_make_readqpacket(struct hpsb_host *host, nodeid_t node,
                                          u64 addr)
{
        struct hpsb_packet *p;

        p = alloc_hpsb_packet(0);
        if (!p) return NULL;

        p->host = host;
        p->tlabel = get_tlabel(host, node, 1);
        p->node_id = node;
        fill_async_readquad(p, addr);

        return p;
}

struct hpsb_packet *hpsb_make_readbpacket(struct hpsb_host *host, nodeid_t node,
                                          u64 addr, size_t length)
{
        struct hpsb_packet *p;

        p = alloc_hpsb_packet(length + (length % 4 ? 4 - (length % 4) : 0));
        if (!p) return NULL;

        p->host = host;
        p->tlabel = get_tlabel(host, node, 1);
        p->node_id = node;
        fill_async_readblock(p, addr, length);

        return p;
}

struct hpsb_packet *hpsb_make_writeqpacket(struct hpsb_host *host,
                                           nodeid_t node, u64 addr,
                                           quadlet_t data)
{
        struct hpsb_packet *p;

        p = alloc_hpsb_packet(0);
        if (!p) return NULL;

        p->host = host;
        p->tlabel = get_tlabel(host, node, 1);
        p->node_id = node;
        fill_async_writequad(p, addr, data);

        return p;
}

struct hpsb_packet *hpsb_make_writebpacket(struct hpsb_host *host,
                                           nodeid_t node, u64 addr,
                                           size_t length)
{
        struct hpsb_packet *p;

        p = alloc_hpsb_packet(length + (length % 4 ? 4 - (length % 4) : 0));
        if (!p) return NULL;

        if (length % 4) {
                p->data[length / 4] = 0;
        }

        p->host = host;
        p->tlabel = get_tlabel(host, node, 1);
        p->node_id = node;
        fill_async_writeblock(p, addr, length);

        return p;
}

struct hpsb_packet *hpsb_make_lockpacket(struct hpsb_host *host, nodeid_t node,
                                         u64 addr, int extcode)
{
        struct hpsb_packet *p;

        p = alloc_hpsb_packet(8);
        if (!p) return NULL;

        p->host = host;
        p->tlabel = get_tlabel(host, node, 1);
        p->node_id = node;

        switch (extcode) {
        case EXTCODE_FETCH_ADD:
        case EXTCODE_LITTLE_ADD:
                fill_async_lock(p, addr, extcode, 4);
                break;
        default:
                fill_async_lock(p, addr, extcode, 8);
                break;
        }

        return p;
}

/*
 * FIXME - these functions should probably read from / write to user space to
 * avoid in kernel buffers for user space callers
 */

int hpsb_read(struct hpsb_host *host, nodeid_t node, u64 addr,
              quadlet_t *buffer, size_t length)
{
        struct hpsb_packet *packet;
        int retval = 0;
        
        if (length == 0) {
                return -EINVAL;
        }

        if (host->node_id == node) {
                switch(highlevel_read(host, node, buffer, addr, length)) {
                case RCODE_COMPLETE:
                        return 0;
                case RCODE_TYPE_ERROR:
                        return -EACCES;
                case RCODE_ADDRESS_ERROR:
                default:
                        return -EINVAL;
                }
        }

        if (length == 4) {
                packet = hpsb_make_readqpacket(host, node, addr);
        } else {
                packet = hpsb_make_readbpacket(host, node, addr, length);
        }

        if (!packet) {
                return -ENOMEM;
        }

        hpsb_send_packet(packet);
        down(&packet->state_change);
        down(&packet->state_change);
        retval = hpsb_packet_success(packet);

        if (retval == 0) {
                if (length == 4) {
                        *buffer = packet->header[3];
                } else {
                        memcpy(buffer, packet->data, length);
                }
        }

        free_tlabel(host, node, packet->tlabel);
        free_hpsb_packet(packet);

        return retval;
}


int hpsb_write(struct hpsb_host *host, nodeid_t node, u64 addr,
               quadlet_t *buffer, size_t length)
{
        struct hpsb_packet *packet;
        int retval = 0;
        
        if (length == 0) {
                return -EINVAL;
        }

        if (host->node_id == node) {
                switch(highlevel_write(host, node, buffer, addr, length)) {
                case RCODE_COMPLETE:
                        return 0;
                case RCODE_TYPE_ERROR:
                        return -EACCES;
                case RCODE_ADDRESS_ERROR:
                default:
                        return -EINVAL;
                }
        }

        if (length == 4) {
                packet = hpsb_make_writeqpacket(host, node, addr, *buffer);
        } else {
                packet = hpsb_make_writebpacket(host, node, addr, length);
        }

        if (!packet) {
                return -ENOMEM;
        }

        if (length != 4) {
                memcpy(packet->data, buffer, length);
        }

        hpsb_send_packet(packet);
        down(&packet->state_change);
        down(&packet->state_change);
        retval = hpsb_packet_success(packet);

        free_tlabel(host, node, packet->tlabel);
        free_hpsb_packet(packet);

        return retval;
}


/* We need a hpsb_lock64 function for the 64 bit equivalent.  Probably. */
int hpsb_lock(struct hpsb_host *host, nodeid_t node, u64 addr, int extcode,
              quadlet_t *data, quadlet_t arg)
{
        struct hpsb_packet *packet;
        int retval = 0, length;
        
        if (host->node_id == node) {
                switch(highlevel_lock(host, node, data, addr, *data, arg,
                                      extcode)) {
                case RCODE_COMPLETE:
                        return 0;
                case RCODE_TYPE_ERROR:
                        return -EACCES;
                case RCODE_ADDRESS_ERROR:
                default:
                        return -EINVAL;
                }
        }

        packet = alloc_hpsb_packet(8);
        if (!packet) {
                return -ENOMEM;
        }

        packet->host = host;
        packet->tlabel = get_tlabel(host, node, 1);
        packet->node_id = node;

        switch (extcode) {
        case EXTCODE_MASK_SWAP:
        case EXTCODE_COMPARE_SWAP:
        case EXTCODE_BOUNDED_ADD:
        case EXTCODE_WRAP_ADD:
                length = 8;
                packet->data[0] = arg;
                packet->data[1] = *data;
                break;
        case EXTCODE_FETCH_ADD:
        case EXTCODE_LITTLE_ADD:
                length = 4;
                packet->data[0] = *data;
                break;
        default:
                return -EINVAL;
        }
        fill_async_lock(packet, addr, extcode, length);

        hpsb_send_packet(packet);
        down(&packet->state_change);
        down(&packet->state_change);
        retval = hpsb_packet_success(packet);

        if (retval == 0) {
                *data = packet->data[0];
        }

        free_tlabel(host, node, packet->tlabel);
        free_hpsb_packet(packet);

        return retval;
}
