
#ifndef _IEEE1394_CORE_H
#define _IEEE1394_CORE_H

#include <linux/tqueue.h>
#include <asm/semaphore.h>
#include "hosts.h"


struct hpsb_packet {
        /* This struct is basically read-only for hosts with the exception of
         * the data buffer contents and xnext - see below. */
        struct list_head list;

        /* This can be used for host driver internal linking. */
        struct hpsb_packet *xnext;

        nodeid_t node_id;

        /* Async and Iso types should be clear, raw means send-as-is, do not
         * CRC!  Byte swapping shall still be done in this case. */
        enum { async, iso, raw } __attribute__((packed)) type;

        /* Okay, this is core internal and a no care for hosts.
         * queued   = queued for sending
         * pending  = sent, waiting for response
         * complete = processing completed, successful or not
         * incoming = incoming packet
         */
        enum { 
                unused, queued, pending, complete, incoming 
        } __attribute__((packed)) state;

        /* These are core internal. */
        char tlabel;
        char ack_code;
        char tcode;

        unsigned expect_response:1;
        unsigned no_waiter:1;

        /* Data big endianness flag - may vary from request to request.  The
         * header is always in machine byte order.
         * Not really used currently.  */
        unsigned data_be:1;

        /* Speed to transmit with: 0 = 100Mbps, 1 = 200Mbps, 2 = 400Mbps */
        unsigned speed_code:2;

        /*
         * *header and *data are guaranteed to be 32-bit DMAable and may be
         * overwritten to allow in-place byte swapping.  Neither of these is
         * CRCed (the sizes also don't include CRC), but contain space for at
         * least one additional quadlet to allow in-place CRCing.  The memory is
         * also guaranteed to be DMA mappable.
         */
        quadlet_t *header;
        quadlet_t *data;
        size_t header_size;
        size_t data_size;


        struct hpsb_host *host;
        unsigned int generation;

        /* Very core internal, don't care. */
        struct semaphore state_change;

        task_queue complete_tq;

        /* Store jiffies for implementing bus timeouts. */
        unsigned long sendtime;
};


void abort_timedouts(struct hpsb_host *host);
void abort_requests(struct hpsb_host *host);

struct hpsb_packet *alloc_hpsb_packet(size_t data_size);
void free_hpsb_packet(struct hpsb_packet *packet);


/*
 * Generation counter for the complete 1394 subsystem.  Generation gets
 * incremented on every change in the subsystem (e.g. bus reset).
 *
 * Use the functions, not the variable.
 */
#include <asm/atomic.h>
extern atomic_t hpsb_generation;

inline static unsigned int get_hpsb_generation(void)
{
        return atomic_read(&hpsb_generation);
}

inline static void inc_hpsb_generation(void)
{
        atomic_inc(&hpsb_generation);
}


/*
 * Queue packet for transmitting, return 0 for failure.
 */
int hpsb_send_packet(struct hpsb_packet *packet);

/* Initiate bus reset on the given host.  Returns 1 if bus reset already in
 * progress, 0 otherwise. */
int hpsb_reset_bus(struct hpsb_host *host);

/*
 * The following functions are exported for host driver module usage.  All of
 * them are safe to use in interrupt contexts, although some are quite
 * complicated so you may want to run them in bottom halves instead of calling
 * them directly.
 */

/* Notify a bus reset to the core.  Returns 1 if bus reset already in progress,
 * 0 otherwise. */
int hpsb_bus_reset(struct hpsb_host *host);

/*
 * Hand over received selfid packet to the core.  Complement check (second
 * quadlet is complement of first) is expected to be done and succesful.
 */
void hpsb_selfid_received(struct hpsb_host *host, quadlet_t sid);

/* 
 * Notify completion of SelfID stage to the core and report new physical ID
 * and whether host is root now.
 */
void hpsb_selfid_complete(struct hpsb_host *host, int phyid, int isroot);

/*
 * Notify core of sending a packet.  Ackcode is the ack code returned for async
 * transmits or ACKX_SEND_ERROR if the transmission failed completely; ACKX_NONE
 * for other cases (internal errors that don't justify a panic).  Safe to call
 * from within a transmit packet routine.
 */
void hpsb_packet_sent(struct hpsb_host *host, struct hpsb_packet *packet,
                      int ackcode);

/*
 * Hand over received packet to the core.  The contents of data are expected to
 * be the full packet but with the CRCs left out (data block follows header
 * immediately), with the header (i.e. the first four quadlets) in machine byte
 * order and the data block in big endian.  *data can be safely overwritten
 * after this call.
 *
 * If the packet is a write request, write_acked is to be set to true if it was
 * ack_complete'd already, false otherwise.  This arg is ignored for any other
 * packet type.
 */
void hpsb_packet_received(struct hpsb_host *host, quadlet_t *data, size_t size,
                          int write_acked);

#endif /* _IEEE1394_CORE_H */
